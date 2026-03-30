#pragma once
// Stale session purge scheduler

#include <cstddef>
#include <cstdint>

namespace tightrope::proxy::session {

class SessionBridge;

class SessionCleanupScheduler {
public:
    explicit SessionCleanupScheduler(std::int64_t interval_ms = 30 * 1000);

    bool should_run(std::int64_t now_ms) const;
    std::size_t run(SessionBridge& bridge, std::int64_t now_ms);

private:
    std::int64_t interval_ms_ = 30 * 1000;
    std::int64_t last_run_ms_ = 0;
};

} // namespace tightrope::proxy::session
