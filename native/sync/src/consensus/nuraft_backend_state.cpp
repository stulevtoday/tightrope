#include "consensus/internal/nuraft_backend_components.h"

#include <exception>

namespace tightrope::sync::consensus::nuraft_backend::internal {

void NoopLogger::put_details(
    const int,
    const char*,
    const char*,
    const std::size_t,
    const std::string&
) {}

InMemoryStateMachine::InMemoryStateMachine(const nuraft::ptr<nuraft::cluster_config>& config)
    : config_(clone_cluster_config(*config)),
      snapshot_(nuraft::cs_new<nuraft::snapshot>(0, 0, clone_cluster_config(*config_))) {}

nuraft::ptr<nuraft::buffer> InMemoryStateMachine::commit(const nuraft::ulong log_idx, nuraft::buffer& data) {
    const auto* bytes = data.data_begin();
    const auto size = data.size();
    std::lock_guard<std::mutex> lock(mutex_);
    committed_payloads_.emplace_back(reinterpret_cast<const char*>(bytes), size);
    commit_index_.store(log_idx);
    return nullptr;
}

bool InMemoryStateMachine::apply_snapshot(nuraft::snapshot& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = nuraft::cs_new<nuraft::snapshot>(
        snapshot.get_last_log_idx(),
        snapshot.get_last_log_term(),
        clone_cluster_config(*snapshot.get_last_config())
    );
    commit_index_.store(snapshot.get_last_log_idx());
    return true;
}

nuraft::ptr<nuraft::snapshot> InMemoryStateMachine::last_snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

nuraft::ulong InMemoryStateMachine::last_commit_index() {
    return commit_index_.load();
}

void InMemoryStateMachine::create_snapshot(
    nuraft::snapshot& snapshot,
    nuraft::async_result<bool>::handler_type& when_done
) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = nuraft::cs_new<nuraft::snapshot>(
            snapshot.get_last_log_idx(),
            snapshot.get_last_log_term(),
            clone_cluster_config(*snapshot.get_last_config())
        );
    }
    bool result = true;
    nuraft::ptr<std::exception> err(nullptr);
    when_done(result, err);
}

std::size_t InMemoryStateMachine::committed_entry_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return committed_payloads_.size();
}

} // namespace tightrope::sync::consensus::nuraft_backend::internal
