#pragma once
// least_outstanding strategy

#include <vector>

#include "eligibility.h"
#include "strategies/strategy.h"

namespace tightrope::balancer {

const AccountCandidate* pick_least_outstanding(CandidateView eligible_accounts);
const AccountCandidate* pick_least_outstanding(const std::vector<AccountCandidate>& accounts,
                                               const EligibilityOptions& options = {});

} // namespace tightrope::balancer
