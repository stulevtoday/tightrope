#pragma once
// latency_ewma strategy

#include <vector>

#include "eligibility.h"
#include "strategies/strategy.h"

namespace tightrope::balancer {

const AccountCandidate* pick_lowest_latency_ewma(CandidateView eligible_accounts);
const AccountCandidate* pick_lowest_latency_ewma(const std::vector<AccountCandidate>& accounts,
                                                 const EligibilityOptions& options = {});

} // namespace tightrope::balancer
