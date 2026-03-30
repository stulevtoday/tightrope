#include "eligibility.h"

#include "math/clamp.h"

namespace tightrope::balancer {

bool is_account_eligible(const AccountCandidate& account, const EligibilityOptions& options) {
    if (options.require_active && !account.active) {
        return false;
    }
    if (options.require_healthy && !account.healthy) {
        return false;
    }
    if (options.require_enabled && !account.enabled) {
        return false;
    }
    return tightrope::core::math::clamp_unit(account.usage_ratio) <=
           tightrope::core::math::clamp_unit(options.max_usage_ratio);
}

CandidateView filter_eligible_accounts(const std::vector<AccountCandidate>& accounts,
                                       const EligibilityOptions& options) {
    CandidateView eligible;
    eligible.reserve(accounts.size());
    for (const auto& account : accounts) {
        if (is_account_eligible(account, options)) {
            eligible.push_back(&account);
        }
    }
    return eligible;
}

} // namespace tightrope::balancer
