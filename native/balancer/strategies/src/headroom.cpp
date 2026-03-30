#include "headroom.h"

#include "algorithm/pick.h"
#include "math/clamp.h"

namespace tightrope::balancer {

double compute_headroom_score(const AccountCandidate& account, const HeadroomWeights& weights) {
    const auto primary_usage = tightrope::core::math::clamp_unit(account.usage_ratio);
    const auto secondary_usage = tightrope::core::math::clamp_unit(
        account.secondary_usage_ratio >= 0.0 ? account.secondary_usage_ratio : account.usage_ratio);

    const auto primary_weight = tightrope::core::math::clamp_non_negative(weights.primary);
    const auto secondary_weight = tightrope::core::math::clamp_non_negative(weights.secondary);
    const auto total_weight = primary_weight + secondary_weight;

    if (total_weight <= 0.0) {
        return 1.0 - primary_usage;
    }

    const auto normalized_primary = primary_weight / total_weight;
    const auto normalized_secondary = secondary_weight / total_weight;
    const auto combined_usage = normalized_primary * primary_usage + normalized_secondary * secondary_usage;
    return tightrope::core::math::clamp_unit(1.0 - combined_usage);
}

const AccountCandidate* pick_headroom_score(CandidateView eligible_accounts, const HeadroomWeights& weights) {
    return tightrope::core::algorithm::pick_max_ptr<AccountCandidate>(
        eligible_accounts,
        [&weights](const AccountCandidate& account) { return compute_headroom_score(account, weights); });
}

const AccountCandidate* pick_headroom_score(const std::vector<AccountCandidate>& accounts,
                                            const EligibilityOptions& options,
                                            const HeadroomWeights& weights) {
    const auto eligible = filter_eligible_accounts(accounts, options);
    return pick_headroom_score(eligible, weights);
}

} // namespace tightrope::balancer
