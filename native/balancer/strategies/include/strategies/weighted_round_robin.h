#pragma once
// weighted_round_robin strategy

#include <string>
#include <unordered_map>
#include <vector>

#include "eligibility.h"
#include "strategies/strategy.h"

namespace tightrope::balancer {

class WeightedRoundRobinPicker {
public:
    const AccountCandidate* pick(CandidateView eligible_accounts);
    const AccountCandidate* pick(const std::vector<AccountCandidate>& accounts,
                                 const EligibilityOptions& options = {});

private:
    std::unordered_map<std::string, double> current_weights_;
};

} // namespace tightrope::balancer
