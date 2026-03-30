#include "consensus/raft_log.h"

#include <algorithm>
#include <string>

#include "consensus/logging.h"

namespace tightrope::sync::consensus {

bool RaftLog::append(const LogEntry& entry) {
    if (entry.index != last_index() + 1 || entry.index == 0) {
        log_consensus_event(
            ConsensusLogLevel::Warning,
            "raft_log",
            "append_rejected",
            "reason=non_contiguous index=" + std::to_string(entry.index) + " expected=" + std::to_string(last_index() + 1)
        );
        return false;
    }
    entries_.push_back(entry);
    log_consensus_event(
        ConsensusLogLevel::Debug,
        "raft_log",
        "append_ok",
        "index=" + std::to_string(entry.index) + " term=" + std::to_string(entry.term)
    );
    return true;
}

bool RaftLog::append_entries(
    const std::uint64_t prev_index,
    const std::uint64_t prev_term,
    const std::vector<LogEntry>& entries
) {
    log_consensus_event(
        ConsensusLogLevel::Debug,
        "raft_log",
        "append_entries_start",
        "prev_index=" + std::to_string(prev_index) + " prev_term=" + std::to_string(prev_term) +
            " incoming=" + std::to_string(entries.size())
    );
    if (prev_index > last_index()) {
        log_consensus_event(
            ConsensusLogLevel::Warning,
            "raft_log",
            "append_entries_rejected",
            "reason=prev_index_out_of_bounds prev_index=" + std::to_string(prev_index) + " last_index=" +
                std::to_string(last_index())
        );
        return false;
    }
    if (prev_index > 0) {
        const auto* prev_entry = entry_at(prev_index);
        if (prev_entry == nullptr || prev_entry->term != prev_term) {
            log_consensus_event(
                ConsensusLogLevel::Warning,
                "raft_log",
                "append_entries_rejected",
                "reason=prev_term_mismatch prev_index=" + std::to_string(prev_index) + " expected_term=" +
                    std::to_string(prev_term)
            );
            return false;
        }
    }

    for (const auto& incoming : entries) {
        if (incoming.index == 0) {
            log_consensus_event(ConsensusLogLevel::Error, "raft_log", "append_entries_rejected", "reason=zero_index");
            return false;
        }

        if (const auto* existing = entry_at(incoming.index); existing != nullptr) {
            if (existing->term != incoming.term) {
                log_consensus_event(
                    ConsensusLogLevel::Info,
                    "raft_log",
                    "conflict_detected",
                    "index=" + std::to_string(incoming.index) + " local_term=" + std::to_string(existing->term) +
                        " remote_term=" + std::to_string(incoming.term)
                );
                truncate_suffix(incoming.index - 1);
                if (!append(incoming)) {
                    return false;
                }
            }
            continue;
        }

        if (!append(incoming)) {
            return false;
        }
    }
    log_consensus_event(
        ConsensusLogLevel::Debug,
        "raft_log",
        "append_entries_ok",
        "last_index=" + std::to_string(last_index())
    );
    return true;
}

void RaftLog::truncate_suffix(const std::uint64_t index) {
    const auto before = entries_.size();
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(), [index](const LogEntry& entry) { return entry.index > index; }),
        entries_.end()
    );
    log_consensus_event(
        ConsensusLogLevel::Info,
        "raft_log",
        "truncate_suffix",
        "keep_up_to=" + std::to_string(index) + " removed=" + std::to_string(before - entries_.size())
    );
}

const LogEntry* RaftLog::entry_at(const std::uint64_t index) const {
    if (index == 0 || index > last_index()) {
        return nullptr;
    }
    return &entries_[static_cast<std::size_t>(index - 1)];
}

std::vector<LogEntry> RaftLog::entries_from(const std::uint64_t index) const {
    std::vector<LogEntry> out;
    if (index == 0) {
        return entries_;
    }
    if (index > last_index()) {
        return out;
    }

    out.reserve(static_cast<std::size_t>(last_index() - index + 1));
    for (std::size_t i = static_cast<std::size_t>(index - 1); i < entries_.size(); ++i) {
        out.push_back(entries_[i]);
    }
    return out;
}

std::uint64_t RaftLog::last_index() const {
    return entries_.empty() ? 0 : entries_.back().index;
}

std::uint64_t RaftLog::last_term() const {
    return entries_.empty() ? 0 : entries_.back().term;
}

} // namespace tightrope::sync::consensus
