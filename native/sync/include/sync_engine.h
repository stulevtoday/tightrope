#pragma once
// Orchestrator: routes writes to Raft or CRDT

#include <cstddef>
#include <cstdint>
#include <string>

#include <sqlite3.h>

#include "sync_protocol.h"

namespace tightrope::sync {

struct ApplyBatchResult {
    bool success = false;
    std::uint64_t applied_up_to_seq = 0;
    std::size_t applied_count = 0;
    std::string error;
};

class SyncEngine {
public:
    static bool recompute_checksums(sqlite3* db);
    static JournalBatchFrame build_batch(sqlite3* db, std::uint64_t after_seq, std::size_t limit);
    static ApplyBatchResult apply_batch(sqlite3* db, const JournalBatchFrame& batch, int applied_value = 2);
};

} // namespace tightrope::sync
