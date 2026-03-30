#include "power_of_two.h"

namespace tightrope::balancer {

PowerOfTwoPicker::PowerOfTwoPicker(const std::uint32_t seed) : rng_(seed) {}

const AccountCandidate* PowerOfTwoPicker::pick(CandidateView eligible_accounts, const ScoreWeights& weights) {
    if (eligible_accounts.empty()) {
        return nullptr;
    }
    if (eligible_accounts.size() == 1) {
        return eligible_accounts.front();
    }

    std::uniform_int_distribution<std::size_t> first_distribution(0, eligible_accounts.size() - 1);
    const auto first_index = first_distribution(rng_);

    std::uniform_int_distribution<std::size_t> second_distribution(0, eligible_accounts.size() - 2);
    std::size_t second_index = second_distribution(rng_);
    if (second_index >= first_index) {
        ++second_index;
    }

    const auto* first = eligible_accounts[first_index];
    const auto* second = eligible_accounts[second_index];
    if (first == nullptr) {
        return second;
    }
    if (second == nullptr) {
        return first;
    }

    const auto first_score = compute_composite_score(*first, weights);
    const auto second_score = compute_composite_score(*second, weights);
    return first_score >= second_score ? first : second;
}

const AccountCandidate* PowerOfTwoPicker::pick(const std::vector<AccountCandidate>& accounts,
                                               const EligibilityOptions& options,
                                               const ScoreWeights& weights) {
    const auto eligible = filter_eligible_accounts(accounts, options);
    return pick(eligible, weights);
}

} // namespace tightrope::balancer
