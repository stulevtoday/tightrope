#include "consensus/raft_scheduler.h"

#include <algorithm>
#include <string>

#include "consensus/logging.h"

namespace tightrope::sync::consensus {

RaftScheduler::RaftScheduler(boost::asio::io_context& io_context)
    : election_timer_(io_context),
      heartbeat_timer_(io_context),
      random_(std::random_device{}()) {}

void RaftScheduler::arm_election_timeout(
    const std::chrono::milliseconds min_timeout,
    const std::chrono::milliseconds max_timeout,
    std::function<void()> on_timeout
) {
    stopped_ = false;
    const auto low = std::min(min_timeout, max_timeout);
    const auto high = std::max(min_timeout, max_timeout);
    std::uniform_int_distribution<std::int64_t> dist(low.count(), high.count());
    const auto selected = std::chrono::milliseconds(dist(random_));

    election_timer_.expires_after(selected);
    log_consensus_event(
        ConsensusLogLevel::Debug,
        "raft_scheduler",
        "arm_election_timeout",
        "min_ms=" + std::to_string(low.count()) + " max_ms=" + std::to_string(high.count()) + " selected_ms=" +
            std::to_string(selected.count())
    );
    election_timer_.async_wait([this, callback = std::move(on_timeout)](const boost::system::error_code& ec) {
        if (ec || stopped_) {
            return;
        }
        log_consensus_event(ConsensusLogLevel::Info, "raft_scheduler", "election_timeout_fired");
        callback();
    });
}

void RaftScheduler::arm_heartbeat(const std::chrono::milliseconds interval, std::function<void()> on_tick) {
    stopped_ = false;
    log_consensus_event(
        ConsensusLogLevel::Debug,
        "raft_scheduler",
        "arm_heartbeat",
        "interval_ms=" + std::to_string(interval.count())
    );
    arm_heartbeat_once(interval, on_tick);
}

void RaftScheduler::cancel_election() {
    election_timer_.cancel();
}

void RaftScheduler::cancel_heartbeat() {
    heartbeat_timer_.cancel();
}

void RaftScheduler::stop() {
    stopped_ = true;
    cancel_election();
    cancel_heartbeat();
    log_consensus_event(ConsensusLogLevel::Info, "raft_scheduler", "stopped");
}

void RaftScheduler::arm_heartbeat_once(const std::chrono::milliseconds interval, const std::function<void()>& on_tick) {
    heartbeat_timer_.expires_after(interval);
    heartbeat_timer_.async_wait([this, interval, on_tick](const boost::system::error_code& ec) {
        if (ec || stopped_) {
            return;
        }
        log_consensus_event(ConsensusLogLevel::Trace, "raft_scheduler", "heartbeat_tick");
        on_tick();
        arm_heartbeat_once(interval, on_tick);
    });
}

} // namespace tightrope::sync::consensus
