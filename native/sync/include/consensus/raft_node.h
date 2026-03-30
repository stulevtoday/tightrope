#pragma once
// Core Raft state machine

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "membership.h"

namespace tightrope::sync::consensus {

namespace nuraft_backend {
class Backend;
}

enum class RaftRole { Follower, Candidate, Leader };

struct RaftState {
    std::uint64_t current_term = 0;
    std::optional<std::uint32_t> voted_for;
    RaftRole role = RaftRole::Follower;
    std::uint32_t leader_id = 0;

    std::uint64_t commit_index = 0;
    std::uint64_t last_applied = 0;

    std::unordered_map<std::uint32_t, std::uint64_t> next_index;
    std::unordered_map<std::uint32_t, std::uint64_t> match_index;
};

struct LogEntryData {
    std::string table_name;
    std::string row_pk;
    std::string op;
    std::string values;
    std::string checksum;
};

class RaftNode {
public:
    RaftNode(std::uint32_t node_id, std::vector<std::uint32_t> peers, std::uint16_t port_base = 26000);
    ~RaftNode();

    RaftNode(const RaftNode&) = delete;
    RaftNode& operator=(const RaftNode&) = delete;

    [[nodiscard]] bool start();
    void stop();
    [[nodiscard]] bool is_running() const;
    [[nodiscard]] std::uint32_t node_id() const noexcept;
    const RaftState& state() const;
    const Membership& membership() const;

    void start_election();
    std::optional<std::uint64_t> propose(const LogEntryData& data);
    [[nodiscard]] std::uint64_t maybe_advance_commit() const;
    [[nodiscard]] std::uint64_t last_log_index() const;
    [[nodiscard]] std::size_t committed_entries() const;

private:
    void refresh_state_cache() const;
    [[nodiscard]] static std::string serialize_log_entry(const LogEntryData& data);

    std::uint32_t node_id_ = 0;
    Membership membership_;
    std::uint16_t port_base_ = 26000;
    mutable RaftState state_{};
    std::unique_ptr<nuraft_backend::Backend> backend_;
};

} // namespace tightrope::sync::consensus
