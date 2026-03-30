#include "scorer.h"

#include "algorithm/pick.h"
#include "math/clamp.h"

#include <limits>

namespace tightrope::balancer {

double usage_weight_from_account(const AccountCandidate& account) {
    const auto usage = tightrope::core::math::clamp_unit(account.usage_ratio);
    const auto headroom = 1.0 - usage;
    const auto base_weight = tightrope::core::math::clamp_non_negative(account.static_weight);
    return headroom * base_weight;
}

double compute_composite_score(const AccountCandidate& account, const ScoreWeights& weights) {
    if (!account.active || !account.healthy || !account.enabled) {
        return -std::numeric_limits<double>::infinity();
    }

    double score = 0.0;
    score += usage_weight_from_account(account) * weights.headroom;
    score += tightrope::core::math::clamp_unit(account.success_rate) * weights.success_rate;
    score -= tightrope::core::math::clamp_non_negative(account.ewma_latency_ms) *
             tightrope::core::math::clamp_non_negative(weights.latency_penalty);
    score -= static_cast<double>(account.outstanding_requests) *
             tightrope::core::math::clamp_non_negative(weights.outstanding_penalty);
    return score;
}

const AccountCandidate* pick_highest_score(CandidateView candidates, const ScoreWeights& weights) {
    return tightrope::core::algorithm::pick_max_ptr<AccountCandidate>(
        candidates, [&weights](const AccountCandidate& candidate) { return compute_composite_score(candidate, weights); });
}

} // namespace tightrope::balancer
