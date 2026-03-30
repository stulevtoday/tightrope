#include <catch2/catch_test_macros.hpp>

#include "hlc.h"

TEST_CASE("hlc local events advance wall and counter monotonically", "[sync][hlc]") {
    tightrope::sync::HlcClock clock(7, 100, 0);

    const auto first = clock.on_local_event(90);
    REQUIRE(first.wall == 100);
    REQUIRE(first.counter == 1);
    REQUIRE(first.site_id == 7);

    const auto second = clock.on_local_event(101);
    REQUIRE(second.wall == 101);
    REQUIRE(second.counter == 2);
    REQUIRE(second.site_id == 7);
}

TEST_CASE("hlc remote events follow max wall and tie-break counter rule", "[sync][hlc]") {
    tightrope::sync::HlcClock clock(9, 200, 5);

    const tightrope::sync::Hlc remote = {
        .wall = 250,
        .counter = 2,
        .site_id = 11,
    };
    const auto merged = clock.on_remote_event(remote, 220);
    REQUIRE(merged.wall == 250);
    REQUIRE(merged.counter == 6);
    REQUIRE(merged.site_id == 9);

    const auto merged_now_wins = clock.on_remote_event(remote, 300);
    REQUIRE(merged_now_wins.wall == 300);
    REQUIRE(merged_now_wins.counter == 0);
}

TEST_CASE("hlc comparison is lexicographic across wall counter site_id", "[sync][hlc]") {
    const tightrope::sync::Hlc a = {.wall = 10, .counter = 3, .site_id = 1};
    const tightrope::sync::Hlc b = {.wall = 10, .counter = 4, .site_id = 1};
    const tightrope::sync::Hlc c = {.wall = 10, .counter = 4, .site_id = 2};

    REQUIRE(tightrope::sync::compare_hlc(a, b) < 0);
    REQUIRE(tightrope::sync::compare_hlc(b, c) < 0);
    REQUIRE(tightrope::sync::compare_hlc(c, c) == 0);
}
