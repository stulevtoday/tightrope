#pragma once
// LWW, field-merge, per-table strategies

#include <string_view>
#include <vector>

#include "hlc.h"

namespace tightrope::sync {

enum class ConflictDecision {
    KeepLocal,
    ApplyRemote,
};

enum class ConflictStrategy {
    RaftLinearizable,
    LwwByHlc,
    CrdtPnCounterMerge,
    CrdtOrSetAddWins,
    LocalOnly,
};

ConflictDecision resolve_lww(const Hlc& remote, const Hlc& local);
ConflictStrategy conflict_strategy_for_table(std::string_view table_name);
bool is_table_replicated(std::string_view table_name);
std::vector<std::string_view> replicated_table_names();
bool table_requires_raft(std::string_view table_name);
ConflictDecision resolve_table_conflict(std::string_view table_name, const Hlc& remote, const Hlc& local);

} // namespace tightrope::sync
