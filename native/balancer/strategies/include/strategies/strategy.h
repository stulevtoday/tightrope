#pragma once
// Strategy interface (template concept)

#include <cstdint>
#include <string>
#include <vector>

namespace tightrope::balancer {

struct AccountCandidate {
    std::string id;
    bool active = true;
    bool healthy = true;
    bool enabled = true;

    // 0.0 => no usage, 1.0 => fully consumed quota.
    double usage_ratio = 0.0;
    // Operator/user tuning weight. Must be >= 0.
    double static_weight = 1.0;

    // Live load/perf hints.
    std::uint32_t outstanding_requests = 0;
    double ewma_latency_ms = 0.0;
    // 0.0 - 1.0.
    double success_rate = 1.0;
    // 0.0 - 1.0 moving error ratio (higher is worse).
    double error_ewma = 0.0;

    // Optional secondary quota signal (0.0 - 1.0). Negative means unset.
    double secondary_usage_ratio = -1.0;
    // Relative per-request cost (typically normalized 0.0 - 1.0).
    double normalized_cost = 0.0;
};

using CandidateView = std::vector<const AccountCandidate*>;

} // namespace tightrope::balancer
