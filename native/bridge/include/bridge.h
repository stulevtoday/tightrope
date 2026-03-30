#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "server.h"
#include "consensus/raft_node.h"
#include "discovery/mdns_publisher.h"
#include "discovery/peer_manager.h"

// N-API bridge class declaration

namespace tightrope::bridge {

struct BridgeConfig {
    std::string host = "127.0.0.1";
    std::uint16_t port = 2455;
    std::string oauth_callback_host = "localhost";
    std::uint16_t oauth_callback_port = 1455;
    std::string db_path;
    std::string config_path;
};

enum class ClusterRole {
    Standalone,
    Follower,
    Candidate,
    Leader,
};

enum class PeerState {
    Connected,
    Disconnected,
    Unreachable,
};

enum class PeerSource {
    Mdns,
    Manual,
};

struct ClusterConfig {
    std::string cluster_name;
    std::uint32_t site_id = 1;
    std::uint16_t sync_port = 0;
    bool discovery_enabled = false;
    std::vector<std::string> manual_peers;
};

struct PeerStatus {
    std::string site_id;
    std::string address;
    PeerState state = PeerState::Disconnected;
    ClusterRole role = ClusterRole::Follower;
    std::uint64_t match_index = 0;
    std::optional<std::uint64_t> last_heartbeat_at;
    PeerSource discovered_via = PeerSource::Manual;
};

struct ClusterStatus {
    bool enabled = false;
    std::string site_id;
    std::string cluster_name;
    ClusterRole role = ClusterRole::Standalone;
    std::uint64_t term = 0;
    std::uint64_t commit_index = 0;
    std::optional<std::string> leader_id;
    std::vector<PeerStatus> peers;
    std::uint64_t journal_entries = 0;
    std::uint64_t pending_raft_entries = 0;
    std::optional<std::uint64_t> last_sync_at;
};

class Bridge {
  public:
    ~Bridge();

    bool init(const BridgeConfig& config) noexcept;
    bool shutdown() noexcept;
    [[nodiscard]] bool is_running() const noexcept;

    bool cluster_enable(const ClusterConfig& config) noexcept;
    bool cluster_disable() noexcept;
    [[nodiscard]] ClusterStatus cluster_status() const noexcept;
    bool cluster_add_peer(const std::string& address) noexcept;
    bool cluster_remove_peer(const std::string& site_id) noexcept;

    bool sync_trigger_now() noexcept;
    bool sync_rollback_batch(const std::string& batch_id) noexcept;

  private:
    struct ActiveCluster {
        ClusterConfig config{};
        mutable sync::discovery::PeerManager peer_manager{"default"};
        std::unique_ptr<sync::discovery::MdnsPublisher> mdns_publisher{};
        std::unique_ptr<sync::consensus::RaftNode> raft_node{};
        std::uint32_t next_peer_site_id = 1;
        std::optional<std::uint64_t> last_sync_at;
    };

    void refresh_cluster_peers(ClusterStatus& status) const noexcept;

    BridgeConfig config_{};
    bool running_ = false;
    bool callback_listener_started_ = false;
    server::Runtime runtime_{};
    std::optional<ActiveCluster> cluster_;
};

} // namespace tightrope::bridge
