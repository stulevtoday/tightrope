#include "least_outstanding.h"

#include "algorithm/pick.h"

namespace tightrope::balancer {

const AccountCandidate* pick_least_outstanding(CandidateView eligible_accounts) {
    return tightrope::core::algorithm::pick_min_ptr<AccountCandidate>(
        eligible_accounts, [](const AccountCandidate& account) { return static_cast<double>(account.outstanding_requests); });
}

const AccountCandidate* pick_least_outstanding(const std::vector<AccountCandidate>& accounts,
                                               const EligibilityOptions& options) {
    const auto eligible = filter_eligible_accounts(accounts, options);
    return pick_least_outstanding(eligible);
}

} // namespace tightrope::balancer
