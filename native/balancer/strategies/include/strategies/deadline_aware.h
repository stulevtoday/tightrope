#pragma once
// deadline_aware strategy

#include <vector>

#include "eligibility.h"
#include "scorer.h"
#include "strategies/strategy.h"

namespace tightrope::balancer {

struct DeadlineAwareOptions {
    // Remaining request slack before user-visible deadline.
    double deadline_slack_ms = 1000.0;
    // At or below this slack we apply maximum urgency scaling.
    double tight_deadline_ms = 250.0;
    // Multiplier for reliability/latency pressure when deadlines are tight.
    double urgency_scale = 2.0;
    ScoreWeights base_weights{};
};

ScoreWeights adjusted_deadline_weights(const DeadlineAwareOptions& options);
const AccountCandidate* pick_deadline_aware(CandidateView eligible_accounts,
                                            const DeadlineAwareOptions& options = {});
const AccountCandidate* pick_deadline_aware(const std::vector<AccountCandidate>& accounts,
                                            const EligibilityOptions& eligibility = {},
                                            const DeadlineAwareOptions& options = {});

} // namespace tightrope::balancer
