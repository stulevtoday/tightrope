#pragma once
// success_rate strategy

#include <cstdint>
#include <random>
#include <vector>

#include "eligibility.h"
#include "strategies/strategy.h"

namespace tightrope::balancer {

double success_probability(const AccountCandidate& account, double epsilon = 1e-6);
double success_rate_weight(const AccountCandidate& account, double rho = 2.0, double epsilon = 1e-6);

class SuccessRateWeightedPicker {
public:
    explicit SuccessRateWeightedPicker(std::uint32_t seed = 0x5eedu);

    const AccountCandidate* pick(CandidateView eligible_accounts, double rho = 2.0, double epsilon = 1e-6);
    const AccountCandidate* pick(const std::vector<AccountCandidate>& accounts,
                                 const EligibilityOptions& options = {},
                                 double rho = 2.0,
                                 double epsilon = 1e-6);

private:
    std::mt19937 rng_;
};

} // namespace tightrope::balancer
