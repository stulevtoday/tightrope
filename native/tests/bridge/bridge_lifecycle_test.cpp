#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "bridge.h"
#include "server/runtime_test_utils.h"

namespace {

tightrope::bridge::BridgeConfig next_bridge_config() {
    static std::atomic<std::uint16_t> port{33100};
    const auto base = static_cast<std::uint16_t>(port.fetch_add(19));
    return {
        .host = "127.0.0.1",
        .port = base,
        .oauth_callback_port = static_cast<std::uint16_t>(base + 1),
    };
}

bool wait_for_cluster_role(
    tightrope::bridge::Bridge& bridge,
    const tightrope::bridge::ClusterRole expected,
    const std::chrono::milliseconds timeout
) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (bridge.cluster_status().role == expected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return bridge.cluster_status().role == expected;
}

bool wait_for_commit_index(
    tightrope::bridge::Bridge& bridge,
    const std::uint64_t at_least,
    const std::chrono::milliseconds timeout
) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (bridge.cluster_status().commit_index >= at_least) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return bridge.cluster_status().commit_index >= at_least;
}

} // namespace

TEST_CASE("bridge init toggles running state", "[bridge][lifecycle]") {
    tightrope::bridge::Bridge bridge;

    REQUIRE_FALSE(bridge.is_running());
    REQUIRE(bridge.init(next_bridge_config()));
    REQUIRE(bridge.is_running());
}

TEST_CASE("bridge shutdown clears running state", "[bridge][lifecycle]") {
    tightrope::bridge::Bridge bridge;

    REQUIRE(bridge.init(next_bridge_config()));
    REQUIRE(bridge.shutdown());
    REQUIRE_FALSE(bridge.is_running());
}

TEST_CASE("bridge shutdown is idempotent", "[bridge][lifecycle]") {
    tightrope::bridge::Bridge bridge;

    REQUIRE(bridge.init(next_bridge_config()));
    REQUIRE(bridge.shutdown());
    REQUIRE(bridge.shutdown());
    REQUIRE_FALSE(bridge.is_running());
}

TEST_CASE("bridge callback listener binds to oauth redirect host", "[bridge][lifecycle][oauth]") {
    tightrope::bridge::Bridge bridge;
    auto config = next_bridge_config();
    config.oauth_callback_host = "localhost";
    REQUIRE(bridge.init(config));

    const auto response = tightrope::tests::server::send_raw_http_to_host(
        "localhost",
        config.oauth_callback_port,
        "GET /auth/callback?code=test&state=invalid HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(response.find("200 OK") != std::string::npos);
    REQUIRE(response.find("OAuth Error") != std::string::npos);
}

TEST_CASE("bridge cluster controls are gated by lifecycle and keep status", "[bridge][lifecycle][cluster]") {
    tightrope::bridge::Bridge bridge;

    REQUIRE_FALSE(bridge.cluster_enable({
        .cluster_name = "alpha",
        .site_id = 7,
        .sync_port = 9001,
        .discovery_enabled = true,
        .manual_peers = {"10.0.0.2:9002"},
    }));

    REQUIRE(bridge.init(next_bridge_config()));
    REQUIRE(bridge.cluster_enable({
        .cluster_name = "alpha",
        .site_id = 7,
        .sync_port = 9001,
        .discovery_enabled = true,
        .manual_peers = {"10.0.0.2:9002"},
    }));

    auto status = bridge.cluster_status();
    REQUIRE(status.enabled);
    REQUIRE(status.cluster_name == "alpha");
    REQUIRE(status.site_id == "7");
    REQUIRE(status.peers.size() == 1);
    REQUIRE(status.peers.front().address == "10.0.0.2:9002");

    REQUIRE(bridge.cluster_add_peer("10.0.0.3:9003"));
    status = bridge.cluster_status();
    REQUIRE(status.peers.size() == 2);

    REQUIRE(bridge.cluster_remove_peer(status.peers.front().site_id));
    status = bridge.cluster_status();
    REQUIRE(status.peers.size() == 1);

    REQUIRE(bridge.cluster_disable());
    REQUIRE_FALSE(bridge.cluster_status().enabled);
}

TEST_CASE("bridge sync controls update cluster status bookkeeping", "[bridge][lifecycle][cluster]") {
    tightrope::bridge::Bridge bridge;
    REQUIRE(bridge.init(next_bridge_config()));
    REQUIRE(bridge.cluster_enable({
        .cluster_name = "alpha",
        .site_id = 3,
        .sync_port = 9001,
        .discovery_enabled = false,
        .manual_peers = {},
    }));

    REQUIRE(bridge.sync_trigger_now());
    const auto after_sync = bridge.cluster_status();
    REQUIRE(after_sync.last_sync_at.has_value());

    REQUIRE(bridge.sync_rollback_batch("batch-1"));
    REQUIRE_FALSE(bridge.sync_rollback_batch(""));
}

TEST_CASE("bridge cluster status is driven by raft state", "[bridge][lifecycle][cluster][raft]") {
    tightrope::bridge::Bridge bridge;
    REQUIRE(bridge.init(next_bridge_config()));
    REQUIRE(bridge.cluster_enable({
        .cluster_name = "raft-driven",
        .site_id = 11,
        .sync_port = 33450,
        .discovery_enabled = false,
        .manual_peers = {},
    }));

    REQUIRE(wait_for_cluster_role(bridge, tightrope::bridge::ClusterRole::Leader, std::chrono::seconds(5)));
    const auto initial = bridge.cluster_status();
    REQUIRE(initial.enabled);
    REQUIRE(initial.term > 0);
    REQUIRE(initial.leader_id.has_value());
    REQUIRE(*initial.leader_id == "11");

    REQUIRE(bridge.sync_trigger_now());
    REQUIRE(wait_for_commit_index(bridge, initial.commit_index + 1, std::chrono::seconds(5)));

    const auto after_sync = bridge.cluster_status();
    REQUIRE(after_sync.commit_index >= initial.commit_index + 1);
    REQUIRE(after_sync.journal_entries >= 1);
    REQUIRE(after_sync.pending_raft_entries == 0);
}
