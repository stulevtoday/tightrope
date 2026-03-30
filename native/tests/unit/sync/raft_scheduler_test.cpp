#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include <boost/asio/io_context.hpp>

#include "consensus/raft_scheduler.h"

using namespace std::chrono_literals;

TEST_CASE("raft scheduler fires randomized election timeout", "[sync][raft][timer]") {
    boost::asio::io_context io;
    tightrope::sync::consensus::RaftScheduler scheduler(io);

    bool fired = false;
    scheduler.arm_election_timeout(2ms, 2ms, [&fired] { fired = true; });

    io.run_for(25ms);
    REQUIRE(fired);
}

TEST_CASE("raft scheduler emits periodic heartbeat ticks", "[sync][raft][timer]") {
    boost::asio::io_context io;
    tightrope::sync::consensus::RaftScheduler scheduler(io);

    int ticks = 0;
    scheduler.arm_heartbeat(2ms, [&ticks] { ++ticks; });

    io.run_for(25ms);
    scheduler.stop();
    REQUIRE(ticks >= 2);
}
