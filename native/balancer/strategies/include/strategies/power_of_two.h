#pragma once
// power_of_two strategy

#include <cstdint>
#include <random>
#include <vector>

#include "eligibility.h"
#include "scorer.h"
#include "strategies/strategy.h"

namespace tightrope::balancer {

class PowerOfTwoPicker {
public:
    explicit PowerOfTwoPicker(std::uint32_t seed = 0x5eedu);

    const AccountCandidate* pick(CandidateView eligible_accounts, const ScoreWeights& weights = {});
    const AccountCandidate* pick(const std::vector<AccountCandidate>& accounts,
                                 const EligibilityOptions& options = {},
                                 const ScoreWeights& weights = {});

private:
    std::mt19937 rng_;
};

} // namespace tightrope::balancer
