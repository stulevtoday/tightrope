#include "hlc.h"

#include <algorithm>

namespace tightrope::sync {

int compare_hlc(const Hlc& lhs, const Hlc& rhs) {
    if (lhs.wall != rhs.wall) {
        return lhs.wall < rhs.wall ? -1 : 1;
    }
    if (lhs.counter != rhs.counter) {
        return lhs.counter < rhs.counter ? -1 : 1;
    }
    if (lhs.site_id != rhs.site_id) {
        return lhs.site_id < rhs.site_id ? -1 : 1;
    }
    return 0;
}

HlcClock::HlcClock(const std::uint32_t site_id, const std::uint64_t wall, const std::uint32_t counter)
    : current_{.wall = wall, .counter = counter, .site_id = site_id} {}

const Hlc& HlcClock::snapshot() const {
    return current_;
}

Hlc HlcClock::on_local_event(const std::uint64_t now_wall_ms) {
    current_.wall = std::max(current_.wall, now_wall_ms);
    ++current_.counter;
    return current_;
}

Hlc HlcClock::on_remote_event(const Hlc& remote, const std::uint64_t now_wall_ms) {
    const auto merged_wall = std::max({current_.wall, remote.wall, now_wall_ms});
    if (merged_wall == remote.wall) {
        current_.counter = std::max(current_.counter, remote.counter) + 1;
    } else {
        current_.counter = 0;
    }
    current_.wall = merged_wall;
    return current_;
}

} // namespace tightrope::sync
