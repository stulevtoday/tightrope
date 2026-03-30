#pragma once
// round_robin strategy

#include <cstddef>
#include <vector>

#include "eligibility.h"
#include "strategies/strategy.h"

namespace tightrope::balancer {

const AccountCandidate* pick_round_robin(CandidateView eligible_accounts, std::size_t& cursor);
const AccountCandidate* pick_round_robin(const std::vector<AccountCandidate>& accounts,
                                         std::size_t& cursor,
                                         const EligibilityOptions& options = {});

} // namespace tightrope::balancer
