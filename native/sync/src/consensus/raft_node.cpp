#include "consensus/raft_node.h"

#include <algorithm>
#include <string>

#include "text/json_escape.h"
#include "consensus/logging.h"
#include "consensus/nuraft_backend.h"

namespace tightrope::sync::consensus {

namespace {

std::vector<std::uint32_t> build_members(const std::uint32_t node_id, const std::vector<std::uint32_t>& peers) {
    std::vector<std::uint32_t> members;
    members.reserve(peers.size() + 1);
    members.push_back(node_id);
    members.insert(members.end(), peers.begin(), peers.end());
    std::sort(members.begin(), members.end());
    members.erase(std::unique(members.begin(), members.end()), members.end());
    return members;
}

} // namespace

RaftNode::RaftNode(const std::uint32_t node_id, std::vector<std::uint32_t> peers, const std::uint16_t port_base)
    : node_id_(node_id),
      membership_(build_members(node_id, peers)),
      port_base_(port_base),
      backend_(std::make_unique<nuraft_backend::Backend>(node_id_, membership_.members(), port_base_)) {}

RaftNode::~RaftNode() {
    stop();
}

bool RaftNode::start() {
    if (!backend_->start()) {
        log_consensus_event(
            ConsensusLogLevel::Error,
            "raft_node",
            "start_failed",
            "node=" + std::to_string(node_id_) + " members=" + std::to_string(membership_.members().size()));
        return false;
    }
    refresh_state_cache();
    log_consensus_event(
        ConsensusLogLevel::Info,
        "raft_node",
        "started",
        "node=" + std::to_string(node_id_) + " leader=" + std::to_string(state_.leader_id) + " term=" +
            std::to_string(state_.current_term));
    return true;
}

void RaftNode::stop() {
    if (backend_ != nullptr) {
        log_consensus_event(
            ConsensusLogLevel::Debug,
            "raft_node",
            "stop_begin",
            "node=" + std::to_string(node_id_));
        backend_->stop();
    }
    refresh_state_cache();
    log_consensus_event(
        ConsensusLogLevel::Debug,
        "raft_node",
        "stop_complete",
        "node=" + std::to_string(node_id_));
}

bool RaftNode::is_running() const {
    return backend_ != nullptr && backend_->is_running();
}

std::uint32_t RaftNode::node_id() const noexcept {
    return node_id_;
}

const RaftState& RaftNode::state() const {
    refresh_state_cache();
    return state_;
}

const Membership& RaftNode::membership() const {
    return membership_;
}

void RaftNode::start_election() {
    if (backend_ == nullptr) {
        log_consensus_event(
            ConsensusLogLevel::Warning,
            "raft_node",
            "start_election_rejected_no_backend",
            "node=" + std::to_string(node_id_));
        return;
    }
    const auto triggered = backend_->trigger_election();
    log_consensus_event(
        ConsensusLogLevel::Debug,
        "raft_node",
        "start_election",
        "node=" + std::to_string(node_id_) + " triggered=" + std::string(triggered ? "1" : "0"));
    refresh_state_cache();
}

std::optional<std::uint64_t> RaftNode::propose(const LogEntryData& data) {
    if (backend_ == nullptr) {
        log_consensus_event(
            ConsensusLogLevel::Warning,
            "raft_node",
            "propose_rejected_no_backend",
            "node=" + std::to_string(node_id_));
        return std::nullopt;
    }
    const auto payload = serialize_log_entry(data);
    auto index = backend_->append_payload(payload);
    if (!index.has_value()) {
        log_consensus_event(
            ConsensusLogLevel::Warning,
            "raft_node",
            "propose_rejected",
            "node=" + std::to_string(node_id_) + " table=" + data.table_name + " row_pk=" + data.row_pk +
                " op=" + data.op);
    } else {
        log_consensus_event(
            ConsensusLogLevel::Debug,
            "raft_node",
            "propose_accepted",
            "node=" + std::to_string(node_id_) + " index=" + std::to_string(*index) + " table=" + data.table_name +
                " row_pk=" + data.row_pk + " op=" + data.op);
    }
    refresh_state_cache();
    return index;
}

std::uint64_t RaftNode::maybe_advance_commit() const {
    if (backend_ == nullptr) {
        return 0;
    }
    return backend_->committed_index();
}

std::uint64_t RaftNode::last_log_index() const {
    if (backend_ == nullptr) {
        return 0;
    }
    return backend_->last_log_index();
}

std::size_t RaftNode::committed_entries() const {
    if (backend_ == nullptr) {
        return 0;
    }
    return backend_->committed_entry_count();
}

void RaftNode::refresh_state_cache() const {
    state_ = {};
    if (backend_ == nullptr || !backend_->is_running()) {
        return;
    }

    state_.current_term = backend_->term();
    const auto leader_id = backend_->leader_id();
    state_.leader_id = leader_id > 0 ? static_cast<std::uint32_t>(leader_id) : 0;
    state_.commit_index = backend_->committed_index();
    state_.last_applied = state_.commit_index;

    if (backend_->is_leader()) {
        state_.role = RaftRole::Leader;
        state_.leader_id = node_id_;
    } else if (state_.leader_id == 0) {
        state_.role = RaftRole::Candidate;
    } else {
        state_.role = RaftRole::Follower;
    }

    if (state_.role == RaftRole::Leader) {
        state_.match_index[node_id_] = backend_->last_log_index();
        state_.next_index[node_id_] = backend_->last_log_index() + 1;
    }
}

std::string RaftNode::serialize_log_entry(const LogEntryData& data) {
    using tightrope::core::text::quote_json_string;
    return "{"
           "\"table_name\":" + quote_json_string(data.table_name) + "," +
           "\"row_pk\":" + quote_json_string(data.row_pk) + "," +
           "\"op\":" + quote_json_string(data.op) + "," +
           "\"values\":" + quote_json_string(data.values) + "," +
           "\"checksum\":" + quote_json_string(data.checksum) +
           "}";
}

} // namespace tightrope::sync::consensus
