#include "cost_aware.h"

#include "algorithm/pick.h"
#include "math/clamp.h"

namespace tightrope::balancer {

const AccountCandidate* pick_cost_aware(CandidateView eligible_accounts, const CostAwareGuardrails& guardrails) {
    CandidateView compliant;
    compliant.reserve(eligible_accounts.size());

    const auto min_headroom = tightrope::core::math::clamp_unit(guardrails.min_headroom);
    const auto max_error = tightrope::core::math::clamp_unit(guardrails.max_error_ewma);

    for (const auto* account : eligible_accounts) {
        if (account == nullptr) {
            continue;
        }
        const auto headroom = compute_headroom_score(*account, guardrails.headroom_weights);
        const auto error_ewma = tightrope::core::math::clamp_unit(account->error_ewma);
        if (headroom >= min_headroom && error_ewma <= max_error) {
            compliant.push_back(account);
        }
    }

    const auto* cheapest = tightrope::core::algorithm::pick_min_ptr<AccountCandidate>(
        compliant, [](const AccountCandidate& account) {
            return tightrope::core::math::clamp_non_negative(account.normalized_cost);
        });
    if (cheapest != nullptr) {
        return cheapest;
    }

    return tightrope::core::algorithm::pick_max_ptr<AccountCandidate>(
        eligible_accounts,
        [&guardrails](const AccountCandidate& account) {
            return compute_headroom_score(account, guardrails.headroom_weights);
        });
}

const AccountCandidate* pick_cost_aware(const std::vector<AccountCandidate>& accounts,
                                        const EligibilityOptions& options,
                                        const CostAwareGuardrails& guardrails) {
    const auto eligible = filter_eligible_accounts(accounts, options);
    return pick_cost_aware(eligible, guardrails);
}

} // namespace tightrope::balancer
