#pragma once
// Hybrid Logical Clock struct, comparison, advancement

#include <cstdint>

namespace tightrope::sync {

struct Hlc {
    std::uint64_t wall = 0;
    std::uint32_t counter = 0;
    std::uint32_t site_id = 0;
};

int compare_hlc(const Hlc& lhs, const Hlc& rhs);

class HlcClock {
public:
    explicit HlcClock(std::uint32_t site_id, std::uint64_t wall = 0, std::uint32_t counter = 0);

    const Hlc& snapshot() const;
    Hlc on_local_event(std::uint64_t now_wall_ms);
    Hlc on_remote_event(const Hlc& remote, std::uint64_t now_wall_ms);

private:
    Hlc current_;
};

} // namespace tightrope::sync
