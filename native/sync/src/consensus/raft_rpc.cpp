#include "consensus/raft_rpc.h"

#include <string>

#include "consensus/logging.h"

namespace tightrope::sync::consensus {

bool is_log_up_to_date(
    const std::uint64_t candidate_last_term,
    const std::uint64_t candidate_last_index,
    const std::uint64_t local_last_term,
    const std::uint64_t local_last_index
) {
    const bool up_to_date = (candidate_last_term != local_last_term) ? (candidate_last_term > local_last_term)
                                                                      : (candidate_last_index >= local_last_index);
    log_consensus_event(
        ConsensusLogLevel::Trace,
        "raft_rpc",
        "compare_log_freshness",
        "candidate_term=" + std::to_string(candidate_last_term) + " candidate_index=" +
            std::to_string(candidate_last_index) + " local_term=" + std::to_string(local_last_term) +
            " local_index=" + std::to_string(local_last_index) + " up_to_date=" + (up_to_date ? "true" : "false")
    );
    if (candidate_last_term != local_last_term) {
        return candidate_last_term > local_last_term;
    }
    return candidate_last_index >= local_last_index;
}

} // namespace tightrope::sync::consensus
