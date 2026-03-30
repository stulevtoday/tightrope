#pragma once
// headroom strategy

#include <vector>

#include "eligibility.h"
#include "strategies/strategy.h"

namespace tightrope::balancer {

struct HeadroomWeights {
    double primary = 0.5;
    double secondary = 0.5;
};

double compute_headroom_score(const AccountCandidate& account, const HeadroomWeights& weights = {});
const AccountCandidate* pick_headroom_score(CandidateView eligible_accounts, const HeadroomWeights& weights = {});
const AccountCandidate* pick_headroom_score(const std::vector<AccountCandidate>& accounts,
                                            const EligibilityOptions& options = {},
                                            const HeadroomWeights& weights = {});

} // namespace tightrope::balancer
