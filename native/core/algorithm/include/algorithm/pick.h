#pragma once

#include <limits>
#include <vector>

namespace tightrope::core::algorithm {

template <typename T, typename ScoreFn>
const T* pick_min_ptr(const std::vector<const T*>& candidates, ScoreFn&& score_fn) {
    const T* best = nullptr;
    double best_score = std::numeric_limits<double>::infinity();

    for (const auto* candidate : candidates) {
        if (candidate == nullptr) {
            continue;
        }
        const auto score = static_cast<double>(score_fn(*candidate));
        if (score < best_score) {
            best = candidate;
            best_score = score;
        }
    }
    return best;
}

template <typename T, typename ScoreFn>
const T* pick_max_ptr(const std::vector<const T*>& candidates, ScoreFn&& score_fn) {
    const T* best = nullptr;
    double best_score = -std::numeric_limits<double>::infinity();

    for (const auto* candidate : candidates) {
        if (candidate == nullptr) {
            continue;
        }
        const auto score = static_cast<double>(score_fn(*candidate));
        if (score > best_score) {
            best = candidate;
            best_score = score;
        }
    }
    return best;
}

} // namespace tightrope::core::algorithm
