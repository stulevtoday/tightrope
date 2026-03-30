#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "session/session_bridge.h"
#include "session/session_cleanup.h"
#include "session/sticky_resolver.h"

TEST_CASE("sticky resolver reuses the same upstream account for a session key", "[proxy][sticky]") {
    tightrope::proxy::session::StickyResolver resolver({"acc-a", "acc-b", "acc-c"});

    const auto first = resolver.pick("session-1");
    const auto second = resolver.pick("session-1");
    const auto third = resolver.pick("session-2");

    REQUIRE_FALSE(first.account_id.empty());
    REQUIRE(first.account_id == second.account_id);
    REQUIRE(second.reused);
    REQUIRE(third.account_id != first.account_id);
}

TEST_CASE("session bridge upserts and resolves by key", "[proxy][sticky]") {
    tightrope::proxy::session::SessionBridge bridge(/*ttl_ms=*/60000);

    bridge.upsert("turn-1", "upstream-session-a", /*now_ms=*/1000);
    const auto* found = bridge.find("turn-1", /*now_ms=*/2000);
    REQUIRE(found != nullptr);
    REQUIRE(found->upstream_session_id == "upstream-session-a");

    bridge.upsert("turn-1", "upstream-session-b", /*now_ms=*/3000);
    const auto* updated = bridge.find("turn-1", /*now_ms=*/3500);
    REQUIRE(updated != nullptr);
    REQUIRE(updated->upstream_session_id == "upstream-session-b");
}

TEST_CASE("session cleanup scheduler purges stale bridge entries", "[proxy][sticky]") {
    tightrope::proxy::session::SessionBridge bridge(/*ttl_ms=*/1000);
    bridge.upsert("fresh", "upstream-fresh", /*now_ms=*/5000);
    bridge.upsert("stale", "upstream-stale", /*now_ms=*/1000);

    tightrope::proxy::session::SessionCleanupScheduler scheduler(/*interval_ms=*/1000);
    REQUIRE_FALSE(scheduler.should_run(/*now_ms=*/500));
    REQUIRE(scheduler.should_run(/*now_ms=*/1500));

    const auto purged = scheduler.run(bridge, /*now_ms=*/5000);
    REQUIRE(purged == 1);
    REQUIRE(bridge.size() == 1);
    REQUIRE(bridge.find("fresh", /*now_ms=*/5000) != nullptr);
    REQUIRE(bridge.find("stale", /*now_ms=*/5000) == nullptr);
}
