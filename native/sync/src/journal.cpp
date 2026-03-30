#include "journal.h"

#include <algorithm>

#include "checksum.h"
#include "journal_batch_id.h"

namespace tightrope::sync {

JournalEntry Journal::append(const PendingJournalEntry& entry) {
    const auto batch_id = entry.batch_id.empty() ? generate_batch_id() : entry.batch_id;
    JournalEntry created = {
        .seq = next_seq_++,
        .hlc = entry.hlc,
        .table_name = entry.table_name,
        .row_pk = entry.row_pk,
        .op = entry.op,
        .old_values = entry.old_values,
        .new_values = entry.new_values,
        .checksum = journal_checksum(entry.table_name, entry.row_pk, entry.op, entry.old_values, entry.new_values),
        .applied = entry.applied,
        .batch_id = batch_id,
    };
    entries_.push_back(created);
    return created;
}

std::vector<JournalEntry> Journal::entries_after(const std::uint64_t seq) const {
    std::vector<JournalEntry> result;
    for (const auto& entry : entries_) {
        if (entry.seq > seq) {
            result.push_back(entry);
        }
    }
    return result;
}

std::vector<JournalEntry> Journal::rollback_batch(const std::string_view batch_id) {
    std::vector<JournalEntry> removed;
    auto it = entries_.begin();
    while (it != entries_.end()) {
        if (it->batch_id == batch_id) {
            removed.push_back(*it);
            it = entries_.erase(it);
            continue;
        }
        ++it;
    }

    std::sort(removed.begin(), removed.end(), [](const JournalEntry& lhs, const JournalEntry& rhs) {
        return lhs.seq > rhs.seq;
    });
    return removed;
}

bool Journal::mark_applied(const std::uint64_t seq, const int applied_value) {
    for (auto& entry : entries_) {
        if (entry.seq == seq) {
            entry.applied = applied_value;
            return true;
        }
    }
    return false;
}

std::size_t Journal::compact(const std::uint64_t cutoff_wall, const std::uint64_t max_ack_seq) {
    const auto before = entries_.size();
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(), [cutoff_wall, max_ack_seq](const JournalEntry& entry) {
            return entry.hlc.wall < cutoff_wall && entry.seq <= max_ack_seq;
        }),
        entries_.end()
    );
    return before - entries_.size();
}

std::size_t Journal::size() const {
    return entries_.size();
}

} // namespace tightrope::sync
