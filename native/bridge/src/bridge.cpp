#include "bridge.h"

#include <chrono>
#include <charconv>
#include <string_view>
#include <thread>
#include <utility>

#include "logging/logger.h"
#include "consensus/raft_node.h"
#include "discovery/mdns_publisher.h"
#include "discovery/peer_endpoint.h"
#include "oauth/callback_server.h"
#include "text/ascii.h"

namespace tightrope::bridge {

namespace {

std::uint64_t now_unix_ms() {
    const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
    return static_cast<std::uint64_t>(now.time_since_epoch().count());
}

constexpr auto kRaftPollInterval = std::chrono::milliseconds(25);
constexpr auto kRaftElectionTimeout = std::chrono::seconds(2);
constexpr int kDiscoveryPollTimeoutMs = 25;
constexpr std::uint64_t kDiscoveryPruneWindowMs = 30'000;

std::string advertise_host_for_mdns(const BridgeConfig& bridge_config) {
    auto host = core::text::trim_ascii(bridge_config.host);
    if (host.empty() || host == "0.0.0.0" || host == "::" || core::text::equals_case_insensitive(host, "localhost")) {
        return "127.0.0.1";
    }
    return host;
}

std::optional<std::uint32_t> parse_positive_u32(const std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }

    std::uint32_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed == 0) {
        return std::nullopt;
    }
    return parsed;
}

ClusterRole to_cluster_role(const sync::consensus::RaftRole role) {
    switch (role) {
    case sync::consensus::RaftRole::Leader:
        return ClusterRole::Leader;
    case sync::consensus::RaftRole::Candidate:
        return ClusterRole::Candidate;
    case sync::consensus::RaftRole::Follower:
    default:
        return ClusterRole::Follower;
    }
}

bool wait_for_raft_leader(sync::consensus::RaftNode& raft, const std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (raft.state().role == sync::consensus::RaftRole::Leader) {
            return true;
        }
        std::this_thread::sleep_for(kRaftPollInterval);
    }
    return raft.state().role == sync::consensus::RaftRole::Leader;
}

sync::consensus::LogEntryData make_sync_log_entry(
    const std::string_view op,
    const std::string_view row_pk,
    const std::string_view values
) {
    return {
        .table_name = "_sync_journal",
        .row_pk = std::string(row_pk),
        .op = std::string(op),
        .values = std::string(values),
        .checksum = "bridge",
    };
}

bool append_sync_entry(sync::consensus::RaftNode& raft, const sync::consensus::LogEntryData& entry) {
    if (raft.propose(entry).has_value()) {
        return true;
    }
    raft.start_election();
    if (!wait_for_raft_leader(raft, kRaftElectionTimeout)) {
        return false;
    }
    return raft.propose(entry).has_value();
}

} // namespace

Bridge::~Bridge() {
    (void)shutdown();
}

bool Bridge::init(const BridgeConfig& config) noexcept {
    if (running_) {
        core::logging::log_event(core::logging::LogLevel::Debug, "runtime", "bridge", "init_ignored", "reason=already_running");
        return true;
    }

    server::RuntimeConfig runtime_config{
        .host = config.host,
        .port = config.port,
    };
    if (!runtime_.start(runtime_config)) {
        running_ = false;
        core::logging::log_event(core::logging::LogLevel::Error, "runtime", "bridge", "init_failed", "reason=runtime_start_failed");
        return false;
    }

    callback_listener_started_ = false;
    if (config.oauth_callback_port != 0 && config.oauth_callback_port != config.port) {
        auth::oauth::CallbackServerConfig callback_config{
            .host = config.oauth_callback_host,
            .port = config.oauth_callback_port,
        };
        if (!auth::oauth::CallbackServer::instance().start(callback_config)) {
            runtime_.stop();
            running_ = false;
            core::logging::log_event(
                core::logging::LogLevel::Error,
                "runtime",
                "bridge",
                "init_failed",
                "reason=oauth_callback_listener_start_failed"
            );
            return false;
        }
        callback_listener_started_ = true;
    }

    config_ = config;
    running_ = true;
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "bridge",
        "init_complete",
        "host=" + config.host + " port=" + std::to_string(config.port) + " oauth_callback_host=" +
            config.oauth_callback_host + " oauth_callback_port=" + std::to_string(config.oauth_callback_port)
    );
    return true;
}

bool Bridge::shutdown() noexcept {
    if (!running_ && !cluster_.has_value() && !callback_listener_started_) {
        return true;
    }

    cluster_disable();
    if (callback_listener_started_) {
        (void)auth::oauth::CallbackServer::instance().stop();
        callback_listener_started_ = false;
    }
    if (running_) {
        runtime_.stop();
    }
    running_ = false;
    core::logging::log_event(core::logging::LogLevel::Info, "runtime", "bridge", "shutdown_complete");
    return true;
}

bool Bridge::is_running() const noexcept {
    return running_ && runtime_.is_running();
}

bool Bridge::cluster_enable(const ClusterConfig& config) noexcept {
    if (!running_ || config.cluster_name.empty() || config.site_id == 0 || config.sync_port == 0) {
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "bridge",
            "cluster_enable_rejected",
            "reason=invalid_state_or_config"
        );
        return false;
    }

    if (cluster_.has_value()) {
        cluster_disable();
    }

    ActiveCluster cluster{
        .config = config,
        .peer_manager = sync::discovery::PeerManager(config.cluster_name),
        .raft_node = std::make_unique<sync::consensus::RaftNode>(config.site_id, std::vector<std::uint32_t>{}, config.sync_port),
        .next_peer_site_id = config.site_id + 1,
        .last_sync_at = std::nullopt,
    };
    if (!cluster.raft_node->start()) {
        core::logging::log_event(core::logging::LogLevel::Error, "runtime", "bridge", "cluster_enable_failed", "reason=raft_start_failed");
        return false;
    }
    cluster.raft_node->start_election();
    (void)wait_for_raft_leader(*cluster.raft_node, kRaftElectionTimeout);

    cluster_ = std::move(cluster);
    if (cluster_->config.discovery_enabled) {
        cluster_->mdns_publisher = std::make_unique<sync::discovery::MdnsPublisher>();
        const sync::discovery::ServiceAnnouncement announcement{
            .cluster_name = cluster_->config.cluster_name,
            .site_id = cluster_->config.site_id,
            .endpoint = {
                .host = advertise_host_for_mdns(config_),
                .port = cluster_->config.sync_port,
            },
        };
        if (!cluster_->mdns_publisher->publish(announcement)) {
            core::logging::log_event(
                core::logging::LogLevel::Warning,
                "runtime",
                "bridge",
                "cluster_mdns_publish_failed",
                "cluster=" + cluster_->config.cluster_name + " site_id=" + std::to_string(cluster_->config.site_id)
            );
        } else {
            core::logging::log_event(
                core::logging::LogLevel::Info,
                "runtime",
                "bridge",
                "cluster_mdns_published",
                "cluster=" + cluster_->config.cluster_name + " site_id=" + std::to_string(cluster_->config.site_id) +
                    " host=" + announcement.endpoint.host + " port=" + std::to_string(announcement.endpoint.port)
            );
        }
    }
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "bridge",
        "cluster_enabled",
        "cluster=" + config.cluster_name + " site_id=" + std::to_string(config.site_id)
    );

    for (const auto& peer : config.manual_peers) {
        if (!cluster_add_peer(peer)) {
            cluster_disable();
            return false;
        }
    }
    return true;
}

bool Bridge::cluster_disable() noexcept {
    if (!cluster_.has_value()) {
        return true;
    }

    if (cluster_->mdns_publisher != nullptr) {
        cluster_->mdns_publisher->unpublish();
        cluster_->mdns_publisher.reset();
    }

    if (cluster_->raft_node != nullptr) {
        cluster_->raft_node->stop();
    }

    cluster_.reset();
    core::logging::log_event(core::logging::LogLevel::Info, "runtime", "bridge", "cluster_disabled");
    return true;
}

ClusterStatus Bridge::cluster_status() const noexcept {
    if (!cluster_.has_value()) {
        return {};
    }

    ClusterStatus status;
    status.enabled = true;
    status.site_id = std::to_string(cluster_->config.site_id);
    status.cluster_name = cluster_->config.cluster_name;
    status.last_sync_at = cluster_->last_sync_at;

    if (cluster_->raft_node != nullptr && cluster_->raft_node->is_running()) {
        const auto& raft_state = cluster_->raft_node->state();
        status.role = to_cluster_role(raft_state.role);
        status.term = raft_state.current_term;
        status.commit_index = raft_state.commit_index;
        status.journal_entries = static_cast<std::uint64_t>(cluster_->raft_node->committed_entries());
        const auto last_log_index = cluster_->raft_node->last_log_index();
        status.pending_raft_entries = last_log_index > raft_state.commit_index
                                        ? last_log_index - raft_state.commit_index
                                        : 0;
        if (raft_state.leader_id > 0) {
            status.leader_id = std::to_string(raft_state.leader_id);
        }
    } else {
        status.role = ClusterRole::Standalone;
    }

    refresh_cluster_peers(status);
    return status;
}

bool Bridge::cluster_add_peer(const std::string& address) noexcept {
    if (!cluster_.has_value()) {
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "bridge", "add_peer_rejected", "reason=cluster_not_enabled");
        return false;
    }

    const auto endpoint = sync::discovery::parse_endpoint(address);
    if (!endpoint.has_value()) {
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "bridge", "add_peer_rejected", "reason=invalid_endpoint");
        return false;
    }

    auto site_id = cluster_->next_peer_site_id;
    while (site_id == cluster_->config.site_id) {
        ++site_id;
    }
    if (!cluster_->peer_manager.add_manual_peer(site_id, *endpoint)) {
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "bridge", "add_peer_rejected", "reason=peer_manager_rejected");
        return false;
    }
    cluster_->next_peer_site_id = site_id + 1;
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "bridge",
        "peer_added",
        "site_id=" + std::to_string(site_id) + " address=" + address
    );
    return true;
}

bool Bridge::cluster_remove_peer(const std::string& site_id) noexcept {
    if (!cluster_.has_value()) {
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "bridge", "remove_peer_rejected", "reason=cluster_not_enabled");
        return false;
    }

    const auto parsed = parse_positive_u32(site_id);
    if (!parsed.has_value()) {
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "bridge", "remove_peer_rejected", "reason=invalid_site_id");
        return false;
    }
    const auto removed = cluster_->peer_manager.remove_peer(*parsed);
    core::logging::log_event(
        removed ? core::logging::LogLevel::Info : core::logging::LogLevel::Warning,
        "runtime",
        "bridge",
        removed ? "peer_removed" : "remove_peer_rejected",
        "site_id=" + std::to_string(*parsed)
    );
    return removed;
}

bool Bridge::sync_trigger_now() noexcept {
    if (!cluster_.has_value() || cluster_->raft_node == nullptr) {
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "bridge", "sync_trigger_rejected", "reason=cluster_not_enabled");
        return false;
    }
    const auto entry = make_sync_log_entry("SYNC_TRIGGER", "{}", "{}");
    if (!append_sync_entry(*cluster_->raft_node, entry)) {
        core::logging::log_event(core::logging::LogLevel::Error, "runtime", "bridge", "sync_trigger_failed", "reason=raft_append_failed");
        return false;
    }
    cluster_->last_sync_at = now_unix_ms();
    core::logging::log_event(core::logging::LogLevel::Info, "runtime", "bridge", "sync_triggered");
    return true;
}

bool Bridge::sync_rollback_batch(const std::string& batch_id) noexcept {
    if (!cluster_.has_value() || cluster_->raft_node == nullptr || batch_id.empty()) {
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "bridge",
            "rollback_rejected",
            "reason=invalid_state_or_batch"
        );
        return false;
    }
    const auto row_pk = std::string("{\"batch_id\":\"") + batch_id + "\"}";
    const auto entry = make_sync_log_entry("ROLLBACK_BATCH", row_pk, "{}");
    if (!append_sync_entry(*cluster_->raft_node, entry)) {
        core::logging::log_event(core::logging::LogLevel::Error, "runtime", "bridge", "rollback_failed", "reason=raft_append_failed");
        return false;
    }
    core::logging::log_event(core::logging::LogLevel::Info, "runtime", "bridge", "rollback_queued", "batch_id=" + batch_id);
    return true;
}

void Bridge::refresh_cluster_peers(ClusterStatus& status) const noexcept {
    status.peers.clear();
    if (!cluster_.has_value()) {
        return;
    }

    if (cluster_->config.discovery_enabled) {
        const auto seen_unix_ms = now_unix_ms();
        (void)cluster_->peer_manager.refresh_discovery(seen_unix_ms, kDiscoveryPollTimeoutMs);
        const auto cutoff_unix_ms =
            seen_unix_ms > kDiscoveryPruneWindowMs ? seen_unix_ms - kDiscoveryPruneWindowMs : 0;
        cluster_->peer_manager.prune_discovered(cutoff_unix_ms);
    }

    const auto peers = cluster_->peer_manager.membership_proposals();
    const auto local_site_id = cluster_->config.site_id;
    const auto leader_site_id = status.leader_id.has_value() ? parse_positive_u32(*status.leader_id) : std::nullopt;
    status.peers.reserve(peers.size());
    for (const auto& peer : peers) {
        if (peer.site_id == local_site_id) {
            continue;
        }
        PeerStatus item;
        item.site_id = std::to_string(peer.site_id);
        item.address = sync::discovery::endpoint_to_string(peer.endpoint);
        item.state = PeerState::Connected;
        item.role = leader_site_id.has_value() && *leader_site_id == peer.site_id
                      ? ClusterRole::Leader
                      : ClusterRole::Follower;
        item.match_index = 0;
        item.last_heartbeat_at = peer.seen_unix_ms > 0 ? std::optional<std::uint64_t>(peer.seen_unix_ms) : std::nullopt;
        item.discovered_via = peer.manual ? PeerSource::Manual : PeerSource::Mdns;
        status.peers.push_back(std::move(item));
    }
}

} // namespace tightrope::bridge
