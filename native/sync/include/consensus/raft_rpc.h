#pragma once
// RequestVote, AppendEntries, InstallSnapshot

#include <cstdint>
#include <vector>

#include "raft_log.h"

namespace tightrope::sync::consensus {

struct RequestVoteRequest {
    std::uint64_t term = 0;
    std::uint32_t candidate_id = 0;
    std::uint64_t last_log_index = 0;
    std::uint64_t last_log_term = 0;
    bool pre_vote = false;
};

struct RequestVoteResponse {
    std::uint64_t term = 0;
    bool vote_granted = false;
};

struct AppendEntriesRequest {
    std::uint64_t term = 0;
    std::uint32_t leader_id = 0;
    std::uint64_t prev_log_index = 0;
    std::uint64_t prev_log_term = 0;
    std::vector<LogEntry> entries;
    std::uint64_t leader_commit = 0;
};

struct AppendEntriesResponse {
    std::uint64_t term = 0;
    bool success = false;
    std::uint64_t match_index = 0;
};

bool is_log_up_to_date(
    std::uint64_t candidate_last_term,
    std::uint64_t candidate_last_index,
    std::uint64_t local_last_term,
    std::uint64_t local_last_index
);

} // namespace tightrope::sync::consensus
