#include "deadline_aware.h"

#include "math/clamp.h"

#include <algorithm>

namespace tightrope::balancer {

ScoreWeights adjusted_deadline_weights(const DeadlineAwareOptions& options) {
    auto adjusted = options.base_weights;

    const auto urgency_scale = std::max(1.0, tightrope::core::math::clamp_non_negative(options.urgency_scale));
    const auto tight_deadline_ms = std::max(1.0, tightrope::core::math::clamp_non_negative(options.tight_deadline_ms));
    const auto slack_ms = tightrope::core::math::clamp_non_negative(options.deadline_slack_ms);

    // urgency: 0 => relaxed, 1 => tight.
    const auto urgency = 1.0 - tightrope::core::math::clamp_unit(slack_ms / tight_deadline_ms);
    const auto factor = 1.0 + urgency * (urgency_scale - 1.0);

    adjusted.success_rate *= factor;
    adjusted.latency_penalty *= factor;
    adjusted.outstanding_penalty *= factor;
    return adjusted;
}

const AccountCandidate* pick_deadline_aware(CandidateView eligible_accounts, const DeadlineAwareOptions& options) {
    return pick_highest_score(eligible_accounts, adjusted_deadline_weights(options));
}

const AccountCandidate* pick_deadline_aware(const std::vector<AccountCandidate>& accounts,
                                            const EligibilityOptions& eligibility,
                                            const DeadlineAwareOptions& options) {
    const auto eligible = filter_eligible_accounts(accounts, eligibility);
    return pick_deadline_aware(eligible, options);
}

} // namespace tightrope::balancer
