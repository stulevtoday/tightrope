#pragma once
// Forward writes to current leader

#include <cstdint>

#include "raft_node.h"

namespace tightrope::sync::consensus {

enum class LeaderProxyAction {
    HandleLocally,
    ForwardToLeader,
    RejectNoQuorum,
    RejectLeaderUnknown,
};

struct LeaderProxyContext {
    std::uint32_t local_node_id = 0;
    std::uint32_t leader_id = 0;
    RaftRole local_role = RaftRole::Follower;
    bool has_quorum = false;
};

struct LeaderProxyDecision {
    LeaderProxyAction action = LeaderProxyAction::RejectLeaderUnknown;
    std::uint32_t target_leader_id = 0;
};

LeaderProxyDecision resolve_leader_proxy(const LeaderProxyContext& context);

} // namespace tightrope::sync::consensus
