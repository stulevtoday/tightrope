#include "latency_ewma.h"

#include "algorithm/pick.h"
#include "math/clamp.h"

namespace tightrope::balancer {

const AccountCandidate* pick_lowest_latency_ewma(CandidateView eligible_accounts) {
    return tightrope::core::algorithm::pick_min_ptr<AccountCandidate>(
        eligible_accounts, [](const AccountCandidate& account) {
            return tightrope::core::math::clamp_non_negative(account.ewma_latency_ms);
        });
}

const AccountCandidate* pick_lowest_latency_ewma(const std::vector<AccountCandidate>& accounts,
                                                 const EligibilityOptions& options) {
    const auto eligible = filter_eligible_accounts(accounts, options);
    return pick_lowest_latency_ewma(eligible);
}

} // namespace tightrope::balancer
