#include <catch2/catch_test_macros.hpp>

#include "consensus/leader_proxy.h"

TEST_CASE("leader proxy handles writes locally on leader", "[sync][raft][proxy]") {
    const auto decision = tightrope::sync::consensus::resolve_leader_proxy({
        .local_node_id = 1,
        .leader_id = 1,
        .local_role = tightrope::sync::consensus::RaftRole::Leader,
        .has_quorum = true,
    });

    REQUIRE(decision.action == tightrope::sync::consensus::LeaderProxyAction::HandleLocally);
}

TEST_CASE("leader proxy forwards writes when follower has known leader", "[sync][raft][proxy]") {
    const auto decision = tightrope::sync::consensus::resolve_leader_proxy({
        .local_node_id = 2,
        .leader_id = 1,
        .local_role = tightrope::sync::consensus::RaftRole::Follower,
        .has_quorum = true,
    });

    REQUIRE(decision.action == tightrope::sync::consensus::LeaderProxyAction::ForwardToLeader);
    REQUIRE(decision.target_leader_id == 1);
}

TEST_CASE("leader proxy rejects writes when quorum unavailable", "[sync][raft][proxy]") {
    const auto decision = tightrope::sync::consensus::resolve_leader_proxy({
        .local_node_id = 2,
        .leader_id = 1,
        .local_role = tightrope::sync::consensus::RaftRole::Follower,
        .has_quorum = false,
    });

    REQUIRE(decision.action == tightrope::sync::consensus::LeaderProxyAction::RejectNoQuorum);
}
