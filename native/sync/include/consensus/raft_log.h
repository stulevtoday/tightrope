#pragma once
// Persistent log storage (raft.db)

#include <cstdint>
#include <string>
#include <vector>

namespace tightrope::sync::consensus {

struct LogEntry {
    std::uint64_t term = 0;
    std::uint64_t index = 0;
    std::string table_name;
    std::string row_pk;
    std::string op;
    std::string values;
    std::string checksum;
};

class RaftLog {
public:
    bool append(const LogEntry& entry);
    bool append_entries(std::uint64_t prev_index, std::uint64_t prev_term, const std::vector<LogEntry>& entries);
    void truncate_suffix(std::uint64_t index);

    const LogEntry* entry_at(std::uint64_t index) const;
    std::vector<LogEntry> entries_from(std::uint64_t index) const;

    std::uint64_t last_index() const;
    std::uint64_t last_term() const;

private:
    std::vector<LogEntry> entries_;
};

} // namespace tightrope::sync::consensus
