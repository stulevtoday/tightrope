#include "discovery/mdns_publisher.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/select.h>
#endif

#include "discovery/mdns_common.h"
#include "discovery/internal/mdns_service_records.h"
#include "discovery/mdns_process_registry.h"
#include "discovery/mdns_runtime.h"
#include "sync_logging.h"

namespace tightrope::sync::discovery {

namespace {

constexpr std::string_view kServiceEnumerationName = "_services._dns-sd._udp.local.";

void log_mdns(const SyncLogLevel level, const std::string_view event, const std::string_view detail = {}) {
    log_discovery_event(level, "mdns_publisher", event, detail);
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

} // namespace

MdnsPublisher::MdnsPublisher(std::string service_name, MdnsRuntime* runtime)
    : service_name_(std::move(service_name)), runtime_(runtime != nullptr ? runtime : &default_mdns_runtime()) {}

MdnsPublisher::~MdnsPublisher() {
    unpublish();
}

bool MdnsPublisher::publish(const ServiceAnnouncement& announcement) {
    if (announcement.cluster_name.empty() || announcement.site_id == 0 || !is_valid_endpoint(announcement.endpoint)) {
        log_mdns(SyncLogLevel::Warning, "publish_rejected_invalid_announcement");
        return false;
    }
    log_mdns(
        SyncLogLevel::Debug,
        "publish_begin",
        "site_id=" + std::to_string(announcement.site_id) + " cluster=" + announcement.cluster_name + " endpoint=" +
            endpoint_to_string(announcement.endpoint));

    unpublish();

    std::uint16_t bound_port = 0;
    const int opened_socket = open_ipv4_mdns_socket(runtime_, &bound_port);
    if (opened_socket < 0) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_ = announcement;
        socket_ = opened_socket;
        running_ = true;
    }

    if (!announce_current()) {
        log_mdns(SyncLogLevel::Error, "announce_failed", "bound_port=" + std::to_string(bound_port));
        unpublish();
        return false;
    }

    register_process_mdns_announcement({
        .service_name = canonical_mdns_service_name(service_name_),
        .cluster_name = announcement.cluster_name,
        .site_id = announcement.site_id,
        .endpoint = announcement.endpoint,
    });

    listener_thread_ = std::thread([this] {
        listen_loop();
    });
    log_mdns(
        SyncLogLevel::Info,
        "published",
        "site_id=" + std::to_string(announcement.site_id) + " cluster=" + announcement.cluster_name + " endpoint=" +
            endpoint_to_string(announcement.endpoint) + " service=" + canonical_mdns_service_name(service_name_) +
            " bound_port=" + std::to_string(bound_port));
    return true;
}

void MdnsPublisher::unpublish() {
    std::optional<ServiceAnnouncement> existing;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!current_.has_value() && socket_ < 0) {
            return;
        }
        existing = current_;
    }
    if (existing.has_value()) {
        log_mdns(
            SyncLogLevel::Debug,
            "unpublish_begin",
            "site_id=" + std::to_string(existing->site_id) + " service=" + canonical_mdns_service_name(service_name_));
    } else {
        log_mdns(SyncLogLevel::Debug, "unpublish_begin", "service=" + canonical_mdns_service_name(service_name_));
    }

    if (existing.has_value()) {
        unregister_process_mdns_announcement(canonical_mdns_service_name(service_name_), existing->site_id);
    }

    (void)goodbye_current();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }

    if (listener_thread_.joinable()) {
        listener_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (socket_ >= 0) {
            runtime_->socket_close(socket_);
            socket_ = -1;
        }
        current_.reset();
    }
    log_mdns(SyncLogLevel::Debug, "unpublished");
}

bool MdnsPublisher::published() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_.has_value() && socket_ >= 0;
}

const std::optional<ServiceAnnouncement>& MdnsPublisher::current() const {
    return current_;
}

const std::string& MdnsPublisher::service_name() const {
    return service_name_;
}

bool MdnsPublisher::announce_current() {
    std::optional<ServiceAnnouncement> announcement;
    int socket = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        announcement = current_;
        socket = socket_;
    }
    if (!announcement.has_value() || socket < 0) {
        log_mdns(SyncLogLevel::Trace, "announce_skipped_no_current");
        return false;
    }

    const auto records = internal::build_service_records(service_name_, *announcement);
    if (!records.has_value()) {
        log_mdns(
            SyncLogLevel::Warning,
            "announce_skipped_invalid_records",
            "site_id=" + std::to_string(announcement->site_id));
        return false;
    }

    MdnsAlignedBuffer buffer{};
    const auto rc = runtime_->announce_multicast(
               socket,
               mdns_buffer_data(buffer),
               mdns_buffer_size(buffer),
               records->ptr_record,
               nullptr,
               0,
               records->additional_records.data(),
               records->additional_count)
        ;
    if (rc < 0) {
        log_mdns(
            SyncLogLevel::Warning,
            "announce_multicast_failed",
            "site_id=" + std::to_string(announcement->site_id) + " rc=" + std::to_string(rc));
        return false;
    }
    log_mdns(
        SyncLogLevel::Trace,
        "announce_multicast_sent",
        "site_id=" + std::to_string(announcement->site_id) + " additional_records=" +
            std::to_string(records->additional_count));
    return true;
}

bool MdnsPublisher::goodbye_current() {
    std::optional<ServiceAnnouncement> announcement;
    int socket = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        announcement = current_;
        socket = socket_;
    }
    if (!announcement.has_value() || socket < 0) {
        log_mdns(SyncLogLevel::Trace, "goodbye_skipped_no_current");
        return false;
    }

    const auto records = internal::build_service_records(service_name_, *announcement);
    if (!records.has_value()) {
        log_mdns(
            SyncLogLevel::Warning,
            "goodbye_skipped_invalid_records",
            "site_id=" + std::to_string(announcement->site_id));
        return false;
    }

    MdnsAlignedBuffer buffer{};
    const auto rc = runtime_->goodbye_multicast(
               socket,
               mdns_buffer_data(buffer),
               mdns_buffer_size(buffer),
               records->ptr_record,
               nullptr,
               0,
               records->additional_records.data(),
               records->additional_count)
        ;
    if (rc < 0) {
        log_mdns(
            SyncLogLevel::Warning,
            "goodbye_multicast_failed",
            "site_id=" + std::to_string(announcement->site_id) + " rc=" + std::to_string(rc));
        return false;
    }
    log_mdns(SyncLogLevel::Trace, "goodbye_multicast_sent", "site_id=" + std::to_string(announcement->site_id));
    return true;
}

int MdnsPublisher::handle_query(
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
    (void)ttl;
    (void)name_length;
    (void)record_offset;
    (void)record_length;

    if (entry != MDNS_ENTRYTYPE_QUESTION) {
        return 0;
    }

    auto* publisher = static_cast<MdnsPublisher*>(user_data);
    if (publisher == nullptr || (rtype != MDNS_RECORDTYPE_PTR && rtype != MDNS_RECORDTYPE_ANY)) {
        return 0;
    }

    std::optional<ServiceAnnouncement> announcement;
    {
        std::lock_guard<std::mutex> lock(publisher->mutex_);
        announcement = publisher->current_;
    }
    if (!announcement.has_value()) {
        return 0;
    }

    const auto records = internal::build_service_records(publisher->service_name_, *announcement);
    if (!records.has_value()) {
        return 0;
    }

    char query_name_buffer[256];
    std::size_t offset = name_offset;
    const auto query_name = mdns_string_extract(data, size, &offset, query_name_buffer, sizeof(query_name_buffer));
    if (query_name.length == 0) {
        return 0;
    }

    const auto normalized_query_name = lowercase_ascii({query_name.str, query_name.length});
    const auto normalized_service_name = lowercase_ascii(records->service_name);
    const auto normalized_enumeration_name = lowercase_ascii(kServiceEnumerationName);
    const bool service_query = normalized_query_name == normalized_service_name;
    const bool enumeration_query = normalized_query_name == normalized_enumeration_name;
    if (!service_query && !enumeration_query) {
        return 0;
    }

    MdnsAlignedBuffer send_buffer{};
    mdns_record_t answer_record = records->ptr_record;
    const char* answer_name = records->service_name.c_str();
    std::size_t answer_name_length = records->service_name.size();
    const mdns_record_t* additional_records = records->additional_records.data();
    std::size_t additional_record_count = records->additional_count;

    std::string enumeration_target;
    if (enumeration_query) {
        enumeration_target = records->service_name;
        answer_record.name = {kServiceEnumerationName.data(), kServiceEnumerationName.size()};
        answer_record.type = MDNS_RECORDTYPE_PTR;
        answer_record.data.ptr.name = {enumeration_target.c_str(), enumeration_target.size()};
        answer_name = kServiceEnumerationName.data();
        answer_name_length = kServiceEnumerationName.size();
        additional_records = nullptr;
        additional_record_count = 0;
    }

    const bool unicast = (rclass & MDNS_UNICAST_RESPONSE) != 0;
    if (unicast) {
        (void)publisher->runtime_->query_answer_unicast(
            sock,
            from,
            addrlen,
            mdns_buffer_data(send_buffer),
            mdns_buffer_size(send_buffer),
            query_id,
            static_cast<mdns_record_type_t>(rtype),
            answer_name,
            answer_name_length,
            answer_record,
            nullptr,
            0,
            additional_records,
            additional_record_count);
    } else {
        (void)publisher->runtime_->query_answer_multicast(
            sock,
            mdns_buffer_data(send_buffer),
            mdns_buffer_size(send_buffer),
            answer_record,
            nullptr,
            0,
            additional_records,
            additional_record_count);
    }

    log_mdns(
        SyncLogLevel::Trace,
        enumeration_query ? "query_answered_service_enumeration" : "query_answered",
        "site_id=" + std::to_string(announcement->site_id) + " query_id=" + std::to_string(query_id) +
            " rtype=" + std::to_string(rtype) + " mode=" + std::string(unicast ? "unicast" : "multicast"));

    return 0;
}

void MdnsPublisher::listen_loop() {
    MdnsAlignedBuffer buffer{};

    while (true) {
        int socket = -1;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) {
                break;
            }
            socket = socket_;
        }

        if (socket < 0) {
            break;
        }

        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(socket, &read_set);

        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100 * 1000;

        const int ready = select(socket + 1, &read_set, nullptr, nullptr, &timeout);
        if (ready > 0 && FD_ISSET(socket, &read_set)) {
            const auto rc = runtime_->socket_listen(
                socket, mdns_buffer_data(buffer), mdns_buffer_size(buffer), &MdnsPublisher::handle_query, this);
            if (rc < 0) {
                log_mdns(SyncLogLevel::Warning, "socket_listen_failed", "rc=" + std::to_string(rc));
            }
        }
    }
}

} // namespace tightrope::sync::discovery
