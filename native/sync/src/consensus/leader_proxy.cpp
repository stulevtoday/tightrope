#include "consensus/leader_proxy.h"

#include <string>

#include "consensus/logging.h"

namespace tightrope::sync::consensus {

LeaderProxyDecision resolve_leader_proxy(const LeaderProxyContext& context) {
    if (!context.has_quorum) {
        log_consensus_event(
            ConsensusLogLevel::Warning,
            "leader_proxy",
            "reject_write",
            "reason=no_quorum local_node=" + std::to_string(context.local_node_id) + " leader=" +
                std::to_string(context.leader_id)
        );
        return {.action = LeaderProxyAction::RejectNoQuorum, .target_leader_id = 0};
    }

    if (context.local_role == RaftRole::Leader || context.local_node_id == context.leader_id) {
        log_consensus_event(
            ConsensusLogLevel::Info,
            "leader_proxy",
            "handle_locally",
            "node=" + std::to_string(context.local_node_id)
        );
        return {.action = LeaderProxyAction::HandleLocally, .target_leader_id = context.local_node_id};
    }

    if (context.leader_id == 0) {
        log_consensus_event(
            ConsensusLogLevel::Warning,
            "leader_proxy",
            "reject_write",
            "reason=leader_unknown local_node=" + std::to_string(context.local_node_id)
        );
        return {.action = LeaderProxyAction::RejectLeaderUnknown, .target_leader_id = 0};
    }

    log_consensus_event(
        ConsensusLogLevel::Info,
        "leader_proxy",
        "forward_to_leader",
        "local_node=" + std::to_string(context.local_node_id) + " leader=" + std::to_string(context.leader_id)
    );
    return {.action = LeaderProxyAction::ForwardToLeader, .target_leader_id = context.leader_id};
}

} // namespace tightrope::sync::consensus
