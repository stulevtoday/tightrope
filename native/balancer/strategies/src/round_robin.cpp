#include "round_robin.h"

namespace tightrope::balancer {

const AccountCandidate* pick_round_robin(CandidateView eligible_accounts, std::size_t& cursor) {
    if (eligible_accounts.empty()) {
        return nullptr;
    }
    const auto index = cursor % eligible_accounts.size();
    const auto* selected = eligible_accounts[index];
    cursor = (index + 1) % eligible_accounts.size();
    return selected;
}

const AccountCandidate* pick_round_robin(const std::vector<AccountCandidate>& accounts,
                                         std::size_t& cursor,
                                         const EligibilityOptions& options) {
    const auto eligible = filter_eligible_accounts(accounts, options);
    return pick_round_robin(eligible, cursor);
}

} // namespace tightrope::balancer
