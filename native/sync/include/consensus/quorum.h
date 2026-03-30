#pragma once

#include <cstddef>

namespace tightrope::sync::consensus {

constexpr std::size_t required_quorum(const std::size_t members) {
    return members == 0 ? 0 : (members / 2) + 1;
}

} // namespace tightrope::sync::consensus
