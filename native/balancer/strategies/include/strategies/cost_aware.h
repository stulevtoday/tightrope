#pragma once
// cost_aware strategy

#include <vector>

#include "eligibility.h"
#include "strategies/headroom.h"
#include "strategies/strategy.h"

namespace tightrope::balancer {

struct CostAwareGuardrails {
    double min_headroom = 0.05;
    double max_error_ewma = 0.50;
    HeadroomWeights headroom_weights{};
};

const AccountCandidate* pick_cost_aware(CandidateView eligible_accounts, const CostAwareGuardrails& guardrails = {});
const AccountCandidate* pick_cost_aware(const std::vector<AccountCandidate>& accounts,
                                        const EligibilityOptions& options = {},
                                        const CostAwareGuardrails& guardrails = {});

} // namespace tightrope::balancer
