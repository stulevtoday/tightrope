#pragma once
// SQLite-backed journal storage.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <sqlite3.h>

#include "journal.h"

namespace tightrope::sync {

class PersistentJournal {
public:
    explicit PersistentJournal(sqlite3* db);

    std::optional<JournalEntry> append(const PendingJournalEntry& entry);
    std::vector<JournalEntry> entries_after(std::uint64_t after_seq, std::size_t limit = 1000) const;
    std::vector<JournalEntry> rollback_batch(std::string_view batch_id);
    bool mark_applied(std::uint64_t seq, int applied_value);
    std::size_t compact(std::uint64_t cutoff_wall, std::uint64_t max_ack_seq);
    std::size_t size() const;

private:
    sqlite3* db_ = nullptr;
};

} // namespace tightrope::sync
