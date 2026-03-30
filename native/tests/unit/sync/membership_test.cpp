#include <catch2/catch_test_macros.hpp>

#include <unordered_set>

#include "consensus/membership.h"

TEST_CASE("membership computes majority for current configuration", "[sync][raft][membership]") {
    tightrope::sync::consensus::Membership membership({1, 2, 3});
    REQUIRE(membership.quorum_size() == 2);

    REQUIRE_FALSE(membership.has_majority(std::unordered_set<std::uint32_t>{1}));
    REQUIRE(membership.has_majority(std::unordered_set<std::uint32_t>{1, 2}));
}

TEST_CASE("membership enforces joint consensus majorities", "[sync][raft][membership]") {
    tightrope::sync::consensus::Membership membership({1, 2, 3});
    REQUIRE(membership.begin_joint_consensus({1, 2, 3, 4}));

    REQUIRE_FALSE(membership.has_joint_majority(std::unordered_set<std::uint32_t>{1, 2}));
    REQUIRE(membership.has_joint_majority(std::unordered_set<std::uint32_t>{1, 2, 3}));

    REQUIRE(membership.commit_joint_consensus());
    REQUIRE(membership.size() == 4);
}
