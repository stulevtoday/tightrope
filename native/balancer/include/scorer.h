#pragma once
// Composite scorer template

#include "strategies/strategy.h"

namespace tightrope::balancer {

struct ScoreWeights {
    double headroom = 1.0;
    double success_rate = 1.0;
    double latency_penalty = 0.001;
    double outstanding_penalty = 0.1;
};

double usage_weight_from_account(const AccountCandidate& account);
double compute_composite_score(const AccountCandidate& account, const ScoreWeights& weights = {});
const AccountCandidate* pick_highest_score(CandidateView candidates, const ScoreWeights& weights = {});

} // namespace tightrope::balancer
