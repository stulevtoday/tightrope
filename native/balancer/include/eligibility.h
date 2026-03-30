#pragma once
// Account eligibility checks

#include <vector>

#include "strategies/strategy.h"

namespace tightrope::balancer {

struct EligibilityOptions {
    double max_usage_ratio = 1.0;
    bool require_active = true;
    bool require_healthy = true;
    bool require_enabled = true;
};

bool is_account_eligible(const AccountCandidate& account, const EligibilityOptions& options = {});
CandidateView filter_eligible_accounts(const std::vector<AccountCandidate>& accounts,
                                       const EligibilityOptions& options = {});

} // namespace tightrope::balancer
