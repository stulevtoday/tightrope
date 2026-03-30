#include "discovery/mdns_browser.h"

#include <array>
#include <chrono>
#include <charconv>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

#include <arpa/inet.h>
#include <sys/select.h>

#include <boost/container/flat_map.hpp>

#include "discovery/mdns_common.h"
#include "discovery/mdns_process_registry.h"
#include "discovery/mdns_runtime.h"
#include "sync_logging.h"

namespace tightrope::sync::discovery {

namespace {

void log_mdns(const SyncLogLevel level, const std::string_view event, const std::string_view detail = {}) {
    log_discovery_event(level, "mdns_browser", event, detail);
}

bool env_truthy(const char* raw) {
    if (raw == nullptr || raw[0] == '\0') {
        return false;
    }
    const std::string value(raw);
    return value == "1" || value == "true" || value == "TRUE" || value == "yes" || value == "on";
}

bool allow_nonstandard_port_fallback() {
    return env_truthy(std::getenv("TIGHTROPE_MDNS_FALLBACK_PORTS"));
}

std::string socket_error_detail(const int err, const std::uint16_t port, const int attempt) {
    std::string detail = "attempt=" + std::to_string(attempt) + " port=" + std::to_string(port) + " errno=" +
                         std::to_string(err);
    if (const char* text = std::strerror(err); text != nullptr) {
        detail += " error=";
        detail += text;
    }
    return detail;
}

int open_ipv4_mdns_socket(MdnsRuntime* runtime, std::uint16_t* bound_port) {
    constexpr int kMaxAttempts = 16;
    const bool allow_fallback = allow_nonstandard_port_fallback();
    int last_errno = 0;
    log_mdns(
        SyncLogLevel::Debug,
        "socket_open_begin",
        "fallback=" + std::string(allow_fallback ? "1" : "0") + " max_attempts=" + std::to_string(kMaxAttempts));

    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (!allow_fallback && attempt > 0) {
            break;
        }

        const auto port = static_cast<std::uint16_t>(MDNS_PORT + attempt);
        sockaddr_in bind_address{};
        bind_address.sin_family = AF_INET;
        bind_address.sin_port = htons(port);
        bind_address.sin_addr.s_addr = INADDR_ANY;
#ifdef __APPLE__
        bind_address.sin_len = sizeof(sockaddr_in);
#endif

        errno = 0;
        const int socket = runtime->socket_open_ipv4(&bind_address);
        if (socket >= 0) {
            if (bound_port != nullptr) {
                *bound_port = port;
            }
            if (attempt > 0) {
                log_mdns(
                    SyncLogLevel::Warning,
                    "non_standard_bind_port",
                    "bound_port=" + std::to_string(port) + " fallback_attempt=" + std::to_string(attempt));
            }
            log_mdns(SyncLogLevel::Debug, "socket_opened", "port=" + std::to_string(port));
            return socket;
        }

        last_errno = errno;
        log_mdns(SyncLogLevel::Warning, "socket_open_failed", socket_error_detail(last_errno, port, attempt));
    }

    log_mdns(
        SyncLogLevel::Error,
        "socket_open_exhausted",
        "attempts=" + std::to_string(allow_fallback ? kMaxAttempts : 1) + " last_errno=" + std::to_string(last_errno));
    return -1;
}

struct PendingPeer {
    std::string instance_name;
    std::string host_name;
    std::optional<std::uint32_t> site_id;
    std::optional<std::uint16_t> sync_port;
    std::optional<std::uint16_t> srv_port;
    std::string cluster_name;
};

struct QueryContext {
    std::string service_name;
    std::uint64_t seen_unix_ms = 0;
    boost::container::flat_map<std::string, PendingPeer> peers_by_instance;
    boost::container::flat_map<std::string, std::string> host_address_by_name;
};

std::string to_string(const mdns_string_t value) {
    if (value.str == nullptr || value.length == 0) {
        return {};
    }
    return {value.str, value.length};
}

std::optional<std::uint32_t> parse_u32(const std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }
    std::uint32_t parsed = 0;
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc() || ptr != value.data() + value.size() || parsed == 0) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::uint16_t> parse_u16(const std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }
    std::uint32_t parsed = 0;
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc() || ptr != value.data() + value.size() || parsed == 0 || parsed > 65535) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(parsed);
}

std::string ipv4_to_string(const sockaddr_in& address) {
    std::array<char, INET_ADDRSTRLEN> buffer{};
    if (inet_ntop(AF_INET, &address.sin_addr, buffer.data(), buffer.size()) == nullptr) {
        return {};
    }
    return std::string(buffer.data());
}

std::string ipv6_to_string(const sockaddr_in6& address) {
    std::array<char, INET6_ADDRSTRLEN> buffer{};
    if (inet_ntop(AF_INET6, &address.sin6_addr, buffer.data(), buffer.size()) == nullptr) {
        return {};
    }
    return std::string(buffer.data());
}

} // namespace

MdnsBrowser::MdnsBrowser(std::string cluster_name, std::string service_name, MdnsRuntime* runtime)
    : cluster_name_(std::move(cluster_name)),
      service_name_(canonical_mdns_service_name(service_name)),
      runtime_(runtime != nullptr ? runtime : &default_mdns_runtime()) {}

MdnsBrowser::~MdnsBrowser() {
    stop();
}

bool MdnsBrowser::ingest(const DiscoveredPeer& peer) {
    if (peer.cluster_name != cluster_name_ || peer.site_id == 0 || !is_valid_endpoint(peer.endpoint)) {
        log_mdns(
            SyncLogLevel::Trace,
            "ingest_rejected",
            "site_id=" + std::to_string(peer.site_id) + " cluster=" + peer.cluster_name);
        return false;
    }

    const auto [it, inserted] = peers_.insert({peer.site_id, peer});
    if (!inserted && peer.seen_unix_ms >= it->second.seen_unix_ms) {
        it->second = peer;
    }
    log_mdns(
        SyncLogLevel::Trace,
        "ingest_applied",
        "site_id=" + std::to_string(peer.site_id) + " endpoint=" + endpoint_to_string(peer.endpoint) + " inserted=" +
            std::string(inserted ? "1" : "0"));
    return true;
}

std::size_t MdnsBrowser::discover_once(const std::uint64_t seen_unix_ms, const int timeout_ms) {
    log_mdns(
        SyncLogLevel::Debug,
        "discover_begin",
        "service=" + service_name_ + " cluster=" + cluster_name_ + " timeout_ms=" + std::to_string(timeout_ms) +
            " seen_unix_ms=" + std::to_string(seen_unix_ms));

    auto ingest_process_announcements = [&]() {
        std::size_t inserted = 0;
        for (const auto& local : process_mdns_announcements(service_name_)) {
            if (ingest({
                    .cluster_name = local.cluster_name,
                    .site_id = local.site_id,
                    .endpoint = local.endpoint,
                    .seen_unix_ms = seen_unix_ms,
                })) {
                ++inserted;
            }
        }
        if (inserted > 0) {
            log_mdns(
                SyncLogLevel::Debug,
                "discover_ingested_process_registry",
                "count=" + std::to_string(inserted) + " service=" + service_name_);
        }
        return inserted;
    };

    if (!ensure_socket()) {
        log_mdns(SyncLogLevel::Warning, "discover_without_socket", "service=" + service_name_);
        return ingest_process_announcements();
    }

    MdnsAlignedBuffer send_buffer{};
    if (runtime_->query_send(
            socket_,
            MDNS_RECORDTYPE_PTR,
            service_name_.c_str(),
            service_name_.size(),
            mdns_buffer_data(send_buffer),
            mdns_buffer_size(send_buffer),
            0)
        < 0) {
        log_mdns(SyncLogLevel::Warning, "query_send_failed", "service=" + service_name_);
        return ingest_process_announcements();
    }
    log_mdns(SyncLogLevel::Trace, "query_sent", "service=" + service_name_);

    QueryContext context;
    context.service_name = lowercase_ascii(service_name_);
    context.seen_unix_ms = seen_unix_ms;

    const auto timeout = std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 1);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    MdnsAlignedBuffer recv_buffer{};

    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            break;
        }

        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(socket_, &read_set);

        timeval tv;
        tv.tv_sec = static_cast<int>(remaining.count() / 1000000);
        tv.tv_usec = static_cast<int>(remaining.count() % 1000000);

        const int ready = select(socket_ + 1, &read_set, nullptr, nullptr, &tv);
        if (ready <= 0) {
            continue;
        }
        if (!FD_ISSET(socket_, &read_set)) {
            continue;
        }

        const auto rc = runtime_->query_recv(
            socket_,
            mdns_buffer_data(recv_buffer),
            mdns_buffer_size(recv_buffer),
            &MdnsBrowser::handle_record,
            &context,
            0);
        if (rc < 0) {
            log_mdns(SyncLogLevel::Warning, "query_recv_failed", "rc=" + std::to_string(rc));
        }
    }

    std::size_t inserted = 0;
    for (const auto& [instance_key, pending] : context.peers_by_instance) {
        (void)instance_key;
        if (!pending.site_id.has_value() || pending.cluster_name.empty()) {
            continue;
        }

        auto resolved_host = pending.host_name;
        const auto host_ip = context.host_address_by_name.find(lowercase_ascii(resolved_host));
        if (host_ip != context.host_address_by_name.end() && !host_ip->second.empty()) {
            resolved_host = host_ip->second;
        } else {
            resolved_host = trim_mdns_trailing_dot(resolved_host);
        }

        const auto resolved_port = pending.sync_port.has_value() ? *pending.sync_port : pending.srv_port.value_or(0);
        if (resolved_host.empty() || resolved_port == 0) {
            continue;
        }

        if (ingest({
                .cluster_name = pending.cluster_name,
                .site_id = *pending.site_id,
                .endpoint = {.host = resolved_host, .port = resolved_port},
                .seen_unix_ms = seen_unix_ms,
            })) {
            ++inserted;
        }
    }

    inserted += ingest_process_announcements();
    log_mdns(
        SyncLogLevel::Debug,
        "discover_complete",
        "inserted=" + std::to_string(inserted) + " candidates=" + std::to_string(context.peers_by_instance.size()) +
            " hosts=" + std::to_string(context.host_address_by_name.size()));

    return inserted;
}

void MdnsBrowser::stop() {
    if (socket_ >= 0) {
        runtime_->socket_close(socket_);
        socket_ = -1;
        log_mdns(SyncLogLevel::Debug, "socket_closed");
    }
}

void MdnsBrowser::prune_older_than(const std::uint64_t cutoff_unix_ms) {
    const auto before = peers_.size();
    for (auto it = peers_.begin(); it != peers_.end();) {
        if (it->second.seen_unix_ms < cutoff_unix_ms) {
            it = peers_.erase(it);
            continue;
        }
        ++it;
    }
    const auto removed = before >= peers_.size() ? before - peers_.size() : 0;
    if (removed > 0) {
        log_mdns(
            SyncLogLevel::Debug,
            "prune_complete",
            "removed=" + std::to_string(removed) + " cutoff_unix_ms=" + std::to_string(cutoff_unix_ms));
    }
}

void MdnsBrowser::clear() {
    peers_.clear();
}

std::vector<DiscoveredPeer> MdnsBrowser::peers() const {
    std::vector<DiscoveredPeer> out;
    out.reserve(peers_.size());
    for (const auto& [_, peer] : peers_) {
        out.push_back(peer);
    }
    return out;
}

const std::string& MdnsBrowser::cluster_name() const {
    return cluster_name_;
}

bool MdnsBrowser::ensure_socket() {
    if (socket_ >= 0) {
        return true;
    }

    std::uint16_t bound_port = 0;
    socket_ = open_ipv4_mdns_socket(runtime_, &bound_port);
    if (socket_ >= 0) {
        log_mdns(
            SyncLogLevel::Debug,
            "socket_ready",
            "service=" + service_name_ + " bound_port=" + std::to_string(bound_port));
    } else {
        log_mdns(SyncLogLevel::Error, "socket_unavailable", "service=" + service_name_);
    }
    return socket_ >= 0;
}

int MdnsBrowser::handle_record(
    const int sock,
    const sockaddr* from,
    const size_t addrlen,
    const mdns_entry_type_t entry,
    const std::uint16_t query_id,
    const std::uint16_t rtype,
    const std::uint16_t rclass,
    const std::uint32_t ttl,
    const void* data,
    const size_t size,
    const size_t name_offset,
    const size_t name_length,
    const size_t record_offset,
    const size_t record_length,
    void* user_data) {
    (void)sock;
    (void)from;
    (void)addrlen;
    (void)entry;
    (void)query_id;
    (void)rclass;
    (void)ttl;
    (void)name_length;

    auto* context = static_cast<QueryContext*>(user_data);
    if (context == nullptr) {
        return 0;
    }

    char name_buffer[256];
    std::size_t offset = name_offset;
    const auto name = mdns_string_extract(data, size, &offset, name_buffer, sizeof(name_buffer));
    const auto entry_name = lowercase_ascii(to_string(name));
    if (entry_name.empty()) {
        return 0;
    }

    if (rtype == MDNS_RECORDTYPE_PTR) {
        char ptr_buffer[256];
        const auto ptr_name = mdns_record_parse_ptr(data, size, record_offset, record_length, ptr_buffer, sizeof(ptr_buffer));
        if (ptr_name.length == 0 || entry_name != context->service_name) {
            return 0;
        }

        const auto instance_name = lowercase_ascii(to_string(ptr_name));
        if (instance_name.empty()) {
            return 0;
        }
        auto& pending = context->peers_by_instance[instance_name];
        pending.instance_name = instance_name;
        log_mdns(SyncLogLevel::Trace, "record_ptr", "instance=" + instance_name);
        return 0;
    }

    if (rtype == MDNS_RECORDTYPE_SRV) {
        char srv_buffer[256];
        const auto srv = mdns_record_parse_srv(data, size, record_offset, record_length, srv_buffer, sizeof(srv_buffer));
        auto& pending = context->peers_by_instance[entry_name];
        pending.instance_name = entry_name;
        pending.host_name = to_string(srv.name);
        pending.srv_port = srv.port;
        log_mdns(
            SyncLogLevel::Trace,
            "record_srv",
            "instance=" + entry_name + " host=" + pending.host_name + " port=" + std::to_string(srv.port));
        return 0;
    }

    if (rtype == MDNS_RECORDTYPE_TXT) {
        std::array<mdns_record_txt_t, 16> txt_records{};
        const auto parsed = mdns_record_parse_txt(data, size, record_offset, record_length, txt_records.data(), txt_records.size());
        auto& pending = context->peers_by_instance[entry_name];
        pending.instance_name = entry_name;
        for (std::size_t index = 0; index < parsed; ++index) {
            const auto key = lowercase_ascii(to_string(txt_records[index].key));
            const auto value = to_string(txt_records[index].value);
            if (key == "cluster") {
                pending.cluster_name = value;
            } else if (key == "site_id") {
                pending.site_id = parse_u32(value);
            } else if (key == "sync_port") {
                pending.sync_port = parse_u16(value);
            }
        }
        log_mdns(
            SyncLogLevel::Trace,
            "record_txt",
            "instance=" + entry_name + " cluster=" + pending.cluster_name + " site_id=" +
                std::to_string(pending.site_id.value_or(0)) + " sync_port=" +
                std::to_string(pending.sync_port.value_or(0)));
        return 0;
    }

    if (rtype == MDNS_RECORDTYPE_A) {
        sockaddr_in address{};
        mdns_record_parse_a(data, size, record_offset, record_length, &address);
        const auto ip = ipv4_to_string(address);
        if (!ip.empty()) {
            context->host_address_by_name[entry_name] = ip;
            log_mdns(SyncLogLevel::Trace, "record_a", "name=" + entry_name + " ip=" + ip);
        }
        return 0;
    }

    if (rtype == MDNS_RECORDTYPE_AAAA) {
        sockaddr_in6 address{};
        mdns_record_parse_aaaa(data, size, record_offset, record_length, &address);
        const auto ip = ipv6_to_string(address);
        if (!ip.empty()) {
            context->host_address_by_name[entry_name] = ip;
            log_mdns(SyncLogLevel::Trace, "record_aaaa", "name=" + entry_name + " ip=" + ip);
        }
        return 0;
    }

    return 0;
}

} // namespace tightrope::sync::discovery
