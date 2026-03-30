#include "weighted_round_robin.h"

#include <limits>
#include <unordered_set>

#include "scorer.h"

namespace tightrope::balancer {

const AccountCandidate* WeightedRoundRobinPicker::pick(CandidateView eligible_accounts) {
    if (eligible_accounts.empty()) {
        return nullptr;
    }

    std::unordered_set<std::string> seen_ids;
    seen_ids.reserve(eligible_accounts.size());

    double total_weight = 0.0;
    double best_current_weight = -std::numeric_limits<double>::infinity();
    const AccountCandidate* best = nullptr;

    for (const auto* account : eligible_accounts) {
        if (account == nullptr) {
            continue;
        }
        seen_ids.insert(account->id);

        const auto weight = usage_weight_from_account(*account);
        if (weight <= 0.0) {
            continue;
        }

        auto& current = current_weights_[account->id];
        current += weight;
        total_weight += weight;

        if (current > best_current_weight) {
            best_current_weight = current;
            best = account;
        }
    }

    if (best == nullptr) {
        return eligible_accounts.front();
    }

    current_weights_[best->id] -= total_weight;

    for (auto it = current_weights_.begin(); it != current_weights_.end();) {
        if (seen_ids.find(it->first) == seen_ids.end()) {
            it = current_weights_.erase(it);
        } else {
            ++it;
        }
    }

    return best;
}

const AccountCandidate* WeightedRoundRobinPicker::pick(const std::vector<AccountCandidate>& accounts,
                                                       const EligibilityOptions& options) {
    const auto eligible = filter_eligible_accounts(accounts, options);
    return pick(eligible);
}

} // namespace tightrope::balancer
