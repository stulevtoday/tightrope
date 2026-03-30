#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "consensus/raft_node.h"

namespace {

using tightrope::sync::consensus::LogEntryData;
using tightrope::sync::consensus::RaftNode;
using tightrope::sync::consensus::RaftRole;

constexpr auto kPollInterval = std::chrono::milliseconds(25);
constexpr auto kClusterStabilizationTimeout = std::chrono::seconds(20);

std::uint16_t next_port_base() {
    static const auto seed = []() {
        const auto ticks = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()
        );
        return static_cast<std::uint16_t>(35000 + (ticks % 20000));
    }();
    static std::atomic<std::uint16_t> base{seed};
    return static_cast<std::uint16_t>(base.fetch_add(20));
}

RaftNode* wait_for_leader(std::vector<RaftNode*>& nodes, const std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    auto next_retry = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() < deadline) {
        for (auto* node : nodes) {
            const auto& state = node->state();
            if (state.role == RaftRole::Leader || state.leader_id == node->node_id()) {
                return node;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_retry) {
            for (auto* node : nodes) {
                node->start_election();
            }
            next_retry = now + std::chrono::milliseconds(250);
        }
        std::this_thread::sleep_for(kPollInterval);
    }
    return nullptr;
}

bool wait_for_committed_index(
    std::vector<RaftNode*>& nodes,
    const std::uint64_t target_index,
    const std::chrono::milliseconds timeout
) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        bool all_committed = true;
        for (auto* node : nodes) {
            if (node->state().commit_index < target_index) {
                all_committed = false;
                break;
            }
        }
        if (all_committed) {
            return true;
        }
        std::this_thread::sleep_for(kPollInterval);
    }
    return false;
}

std::size_t committed_node_count(std::vector<RaftNode*>& nodes, const std::uint64_t target_index) {
    std::size_t count = 0;
    for (auto* node : nodes) {
        if (node->state().commit_index >= target_index) {
            ++count;
        }
    }
    return count;
}

LogEntryData make_data() {
    return {
        .table_name = "accounts",
        .row_pk = R"({"id":"1"})",
        .op = "INSERT",
        .values = R"({"email":"a@x.com"})",
        .checksum = "digest",
    };
}

} // namespace

TEST_CASE("nuraft node starts and elects a leader", "[sync][raft][node]") {
    const auto port_base = next_port_base();

    RaftNode node1(1, {2, 3}, port_base);
    RaftNode node2(2, {1, 3}, port_base);
    RaftNode node3(3, {1, 2}, port_base);

    if (!node1.start() || !node2.start() || !node3.start()) {
        SKIP("NuRaft runtime unavailable on this environment");
    }

    node1.start_election();
    std::vector<RaftNode*> nodes = {&node1, &node2, &node3};
    auto* leader = wait_for_leader(nodes, kClusterStabilizationTimeout);
    REQUIRE(leader != nullptr);
    REQUIRE(leader->is_running());
    const auto leader_state = leader->state();
    REQUIRE((leader_state.role == RaftRole::Leader || leader_state.leader_id == leader->node_id()));

    node1.stop();
    node2.stop();
    node3.stop();
}

TEST_CASE("nuraft cluster commits leader proposals and rejects follower writes", "[sync][raft][node]") {
    const auto port_base = next_port_base();

    RaftNode node1(1, {2, 3}, port_base);
    RaftNode node2(2, {1, 3}, port_base);
    RaftNode node3(3, {1, 2}, port_base);

    if (!node1.start() || !node2.start() || !node3.start()) {
        SKIP("NuRaft runtime unavailable on this environment");
    }

    node2.start_election();
    std::vector<RaftNode*> nodes = {&node1, &node2, &node3};
    auto* leader = wait_for_leader(nodes, kClusterStabilizationTimeout);
    REQUIRE(leader != nullptr);

    for (auto* node : nodes) {
        if (node == leader) {
            continue;
        }
        REQUIRE_FALSE(node->propose(make_data()).has_value());
    }

    const auto index = leader->propose(make_data());
    REQUIRE(index.has_value());
    REQUIRE(*index >= 1);
    REQUIRE(wait_for_committed_index(nodes, *index, kClusterStabilizationTimeout));
    REQUIRE(committed_node_count(nodes, *index) >= 2);
    REQUIRE(leader->state().commit_index >= *index);

    for (auto* node : nodes) {
        if (node->state().commit_index >= *index) {
            REQUIRE(node->committed_entries() >= 1);
        }
    }

    node1.stop();
    node2.stop();
    node3.stop();
}

TEST_CASE("nuraft sqlite persistence keeps raft log across restart", "[sync][raft][node]") {
    const auto port_base = next_port_base();

    RaftNode node(1, {}, port_base);
    if (!node.start()) {
        SKIP("NuRaft runtime unavailable on this environment");
    }

    std::vector<RaftNode*> nodes = {&node};
    node.start_election();
    auto* leader = wait_for_leader(nodes, kClusterStabilizationTimeout);
    REQUIRE(leader == &node);

    const auto index = node.propose(make_data());
    REQUIRE(index.has_value());
    REQUIRE(wait_for_committed_index(nodes, *index, kClusterStabilizationTimeout));
    node.stop();

    if (!node.start()) {
        SKIP("NuRaft runtime unavailable on this environment");
    }
    node.start_election();
    leader = wait_for_leader(nodes, kClusterStabilizationTimeout);
    REQUIRE(leader == &node);
    REQUIRE(node.last_log_index() >= *index);

    node.stop();
}
