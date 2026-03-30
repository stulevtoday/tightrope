#include "session_cleanup.h"

#include <algorithm>

#include "session_bridge.h"

namespace tightrope::proxy::session {

SessionCleanupScheduler::SessionCleanupScheduler(const std::int64_t interval_ms)
    : interval_ms_(std::max<std::int64_t>(1, interval_ms)) {}

bool SessionCleanupScheduler::should_run(const std::int64_t now_ms) const {
    if (now_ms < last_run_ms_) {
        return false;
    }
    return (now_ms - last_run_ms_) >= interval_ms_;
}

std::size_t SessionCleanupScheduler::run(SessionBridge& bridge, const std::int64_t now_ms) {
    if (!should_run(now_ms)) {
        return 0;
    }
    last_run_ms_ = now_ms;
    return bridge.purge_stale(now_ms);
}

} // namespace tightrope::proxy::session
