#pragma once
// Journal read/write/compact

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "hlc.h"

namespace tightrope::sync {

struct PendingJournalEntry {
    Hlc hlc;
    std::string table_name;
    std::string row_pk;
    std::string op;
    std::string old_values;
    std::string new_values;
    int applied = 1;
    std::string batch_id;
};

struct JournalEntry {
    std::uint64_t seq = 0;
    Hlc hlc;
    std::string table_name;
    std::string row_pk;
    std::string op;
    std::string old_values;
    std::string new_values;
    std::string checksum;
    int applied = 1;
    std::string batch_id;
};

class Journal {
public:
    JournalEntry append(const PendingJournalEntry& entry);
    std::vector<JournalEntry> entries_after(std::uint64_t seq) const;
    std::vector<JournalEntry> rollback_batch(std::string_view batch_id);
    bool mark_applied(std::uint64_t seq, int applied_value);
    std::size_t compact(std::uint64_t cutoff_wall, std::uint64_t max_ack_seq);
    std::size_t size() const;

private:
    std::uint64_t next_seq_ = 1;
    std::vector<JournalEntry> entries_;
};

} // namespace tightrope::sync
