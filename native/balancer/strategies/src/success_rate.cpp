#include "success_rate.h"

#include "math/clamp.h"

#include <cmath>
#include <vector>

namespace tightrope::balancer {

double success_probability(const AccountCandidate& account, const double epsilon) {
    const auto clamped_epsilon = tightrope::core::math::clamp_non_negative(epsilon);
    const auto p_success = account.success_rate;
    if (p_success > 0.0) {
        return tightrope::core::math::clamp_unit(p_success);
    }
    const auto error = tightrope::core::math::clamp_unit(account.error_ewma);
    return std::max(clamped_epsilon, 1.0 - error);
}

double success_rate_weight(const AccountCandidate& account, const double rho, const double epsilon) {
    const auto clamped_epsilon = std::max(1e-12, tightrope::core::math::clamp_non_negative(epsilon));
    const auto clamped_rho = std::max(0.1, tightrope::core::math::clamp_non_negative(rho));
    const auto p_success = std::max(clamped_epsilon, success_probability(account, clamped_epsilon));
    return std::max(clamped_epsilon, std::pow(p_success, clamped_rho));
}

SuccessRateWeightedPicker::SuccessRateWeightedPicker(const std::uint32_t seed) : rng_(seed) {}

const AccountCandidate* SuccessRateWeightedPicker::pick(CandidateView eligible_accounts,
                                                        const double rho,
                                                        const double epsilon) {
    if (eligible_accounts.empty()) {
        return nullptr;
    }

    std::vector<double> weights;
    weights.reserve(eligible_accounts.size());

    double total_weight = 0.0;
    for (const auto* account : eligible_accounts) {
        if (account == nullptr) {
            weights.push_back(0.0);
            continue;
        }
        const auto weight = success_rate_weight(*account, rho, epsilon);
        weights.push_back(weight);
        total_weight += weight;
    }

    if (total_weight <= 0.0) {
        return eligible_accounts.front();
    }

    std::uniform_real_distribution<double> distribution(0.0, total_weight);
    const auto target = distribution(rng_);

    double cumulative = 0.0;
    for (std::size_t i = 0; i < eligible_accounts.size(); ++i) {
        cumulative += weights[i];
        if (target <= cumulative && eligible_accounts[i] != nullptr) {
            return eligible_accounts[i];
        }
    }
    return eligible_accounts.back();
}

const AccountCandidate* SuccessRateWeightedPicker::pick(const std::vector<AccountCandidate>& accounts,
                                                        const EligibilityOptions& options,
                                                        const double rho,
                                                        const double epsilon) {
    const auto eligible = filter_eligible_accounts(accounts, options);
    return pick(eligible, rho, epsilon);
}

} // namespace tightrope::balancer
