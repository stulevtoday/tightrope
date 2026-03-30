#pragma once

#include <chrono>
#include <functional>
#include <random>

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

namespace tightrope::sync::consensus {

class RaftScheduler {
public:
    explicit RaftScheduler(boost::asio::io_context& io_context);

    void arm_election_timeout(
        std::chrono::milliseconds min_timeout,
        std::chrono::milliseconds max_timeout,
        std::function<void()> on_timeout
    );
    void arm_heartbeat(std::chrono::milliseconds interval, std::function<void()> on_tick);

    void cancel_election();
    void cancel_heartbeat();
    void stop();

private:
    void arm_heartbeat_once(std::chrono::milliseconds interval, const std::function<void()>& on_tick);

    boost::asio::steady_timer election_timer_;
    boost::asio::steady_timer heartbeat_timer_;
    std::mt19937 random_;
    bool stopped_ = false;
};

} // namespace tightrope::sync::consensus
