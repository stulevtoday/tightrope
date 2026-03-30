#include "consensus/membership.h"

#include <algorithm>
#include <string>

#include "consensus/logging.h"
#include "consensus/quorum.h"

namespace tightrope::sync::consensus {

Membership::Membership(std::vector<std::uint32_t> members)
    : members_(normalize(std::move(members))) {
    log_consensus_event(
        ConsensusLogLevel::Info,
        "membership",
        "initialized",
        "members=" + std::to_string(members_.size()) + " quorum=" + std::to_string(quorum_size())
    );
}

std::size_t Membership::size() const {
    return members_.size();
}

std::size_t Membership::quorum_size() const {
    return required_quorum(members_.size());
}

const std::vector<std::uint32_t>& Membership::members() const {
    return members_;
}

bool Membership::contains(const std::uint32_t node_id) const {
    return std::find(members_.begin(), members_.end(), node_id) != members_.end();
}

bool Membership::has_majority(const std::unordered_set<std::uint32_t>& votes) const {
    return has_majority_for(members_, votes);
}

bool Membership::has_joint_majority(const std::unordered_set<std::uint32_t>& votes) const {
    if (!joint_members_.has_value()) {
        return has_majority(votes);
    }
    return has_majority_for(members_, votes) && has_majority_for(*joint_members_, votes);
}

bool Membership::begin_joint_consensus(std::vector<std::uint32_t> next_members) {
    if (joint_members_.has_value()) {
        log_consensus_event(
            ConsensusLogLevel::Warning,
            "membership",
            "begin_joint_consensus_rejected",
            "reason=already_in_joint_consensus"
        );
        return false;
    }
    auto normalized = normalize(std::move(next_members));
    if (normalized.empty()) {
        log_consensus_event(
            ConsensusLogLevel::Warning,
            "membership",
            "begin_joint_consensus_rejected",
            "reason=empty_next_members"
        );
        return false;
    }
    joint_members_ = std::move(normalized);
    log_consensus_event(
        ConsensusLogLevel::Info,
        "membership",
        "begin_joint_consensus",
        "current=" + std::to_string(members_.size()) + " next=" + std::to_string(joint_members_->size())
    );
    return true;
}

bool Membership::commit_joint_consensus() {
    if (!joint_members_.has_value()) {
        log_consensus_event(
            ConsensusLogLevel::Warning,
            "membership",
            "commit_joint_consensus_rejected",
            "reason=no_pending_joint_config"
        );
        return false;
    }
    members_ = std::move(*joint_members_);
    joint_members_.reset();
    log_consensus_event(
        ConsensusLogLevel::Info,
        "membership",
        "commit_joint_consensus",
        "members=" + std::to_string(members_.size()) + " quorum=" + std::to_string(quorum_size())
    );
    return true;
}

bool Membership::in_joint_consensus() const {
    return joint_members_.has_value();
}

std::vector<std::uint32_t> Membership::normalize(std::vector<std::uint32_t> members) {
    members.erase(std::remove(members.begin(), members.end(), 0), members.end());
    std::sort(members.begin(), members.end());
    members.erase(std::unique(members.begin(), members.end()), members.end());
    return members;
}

bool Membership::has_majority_for(
    const std::vector<std::uint32_t>& members,
    const std::unordered_set<std::uint32_t>& votes
) {
    std::size_t granted = 0;
    for (const auto member : members) {
        if (votes.contains(member)) {
            ++granted;
        }
    }
    return granted >= required_quorum(members.size());
}

} // namespace tightrope::sync::consensus
