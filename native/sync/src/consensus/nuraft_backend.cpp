#include "consensus/nuraft_backend.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "consensus/internal/nuraft_backend_components.h"
#include "consensus/logging.h"

namespace tightrope::sync::consensus::nuraft_backend {

class Backend::Impl {
public:
    Impl(std::uint32_t node_id, std::vector<std::uint32_t> members, std::uint16_t port_base)
        : node_id_(node_id),
          members_(internal::normalize_members(std::move(members))),
          port_base_(port_base),
          storage_path_(internal::make_storage_path(node_id, port_base)) {}

    std::uint32_t node_id_ = 0;
    std::vector<std::uint32_t> members_;
    std::uint16_t port_base_ = 26000;
    std::string storage_path_;
    bool running_ = false;

    nuraft::raft_launcher launcher_;
    nuraft::ptr<nuraft::raft_server> raft_server_;
    nuraft::ptr<internal::InMemoryStateMachine> state_machine_;
    nuraft::ptr<internal::SqliteStateManager> state_manager_;
    std::shared_ptr<internal::SqliteRaftStorage> storage_;
    nuraft::ptr<internal::NoopLogger> logger_;
};

namespace {

std::string members_to_string(const std::vector<std::uint32_t>& members) {
    std::string out;
    out.push_back('[');
    for (std::size_t index = 0; index < members.size(); ++index) {
        if (index > 0) {
            out.push_back(',');
        }
        out.append(std::to_string(members[index]));
    }
    out.push_back(']');
    return out;
}

} // namespace

Backend::Backend(const std::uint32_t node_id, std::vector<std::uint32_t> members, const std::uint16_t port_base)
    : impl_(new Impl(node_id, std::move(members), port_base)) {}

Backend::~Backend() {
    stop();
    delete impl_;
    impl_ = nullptr;
}

bool Backend::start() {
    if (impl_->running_) {
        return true;
    }

    log_consensus_event(
        ConsensusLogLevel::Debug,
        "nuraft_backend",
        "start_begin",
        "node=" + std::to_string(impl_->node_id_) + " members=" + members_to_string(impl_->members_) +
            " port_base=" + std::to_string(impl_->port_base_) + " storage=" + impl_->storage_path_);

    const auto config = internal::build_cluster_config(impl_->members_, impl_->port_base_);
    impl_->storage_ = std::make_shared<internal::SqliteRaftStorage>(impl_->storage_path_);
    if (!impl_->storage_->open()) {
        log_consensus_event(
            ConsensusLogLevel::Error,
            "nuraft_backend",
            "start_failed_storage_open",
            "node=" + std::to_string(impl_->node_id_) + " path=" + impl_->storage_path_ +
                " error=" + impl_->storage_->last_error());
        impl_->storage_.reset();
        return false;
    }

    auto log_store = nuraft::cs_new<internal::SqliteLogStore>(impl_->storage_);
    impl_->state_machine_ = nuraft::cs_new<internal::InMemoryStateMachine>(config);
    impl_->state_manager_ =
        nuraft::cs_new<internal::SqliteStateManager>(impl_->node_id_, config, log_store, impl_->storage_);
    impl_->logger_ = nuraft::cs_new<internal::NoopLogger>();

    nuraft::raft_params params;
    params.with_election_timeout_lower(150)
        .with_election_timeout_upper(350)
        .with_hb_interval(75)
        .with_rpc_failure_backoff(50)
        .with_max_append_size(64);

    nuraft::asio_service::options asio_options;
    asio_options.thread_pool_size_ = 2;

    nuraft::raft_server::init_options init_options;
    init_options.skip_initial_election_timeout_ = false;
    init_options.start_server_in_constructor_ = true;
    init_options.test_mode_flag_ = true;

    const auto port = static_cast<int>(impl_->port_base_ + impl_->node_id_);
    try {
        impl_->raft_server_ = impl_->launcher_.init(
            impl_->state_machine_,
            impl_->state_manager_,
            impl_->logger_,
            port,
            asio_options,
            params,
            init_options
        );
    } catch (const std::exception& ex) {
        log_consensus_event(
            ConsensusLogLevel::Error,
            "nuraft_backend",
            "start_failed_launcher_exception",
            "node=" + std::to_string(impl_->node_id_) + " port=" + std::to_string(port) + " error=" + ex.what());
        impl_->raft_server_.reset();
    } catch (...) {
        log_consensus_event(
            ConsensusLogLevel::Error,
            "nuraft_backend",
            "start_failed_launcher_exception",
            "node=" + std::to_string(impl_->node_id_) + " port=" + std::to_string(port) + " error=unknown");
        impl_->raft_server_.reset();
    }

    if (!impl_->raft_server_) {
        log_consensus_event(
            ConsensusLogLevel::Error,
            "nuraft_backend",
            "start_failed_launcher_init",
            "node=" + std::to_string(impl_->node_id_) + " port=" + std::to_string(port) + " members=" +
                members_to_string(impl_->members_));
        impl_->state_machine_.reset();
        impl_->state_manager_.reset();
        impl_->logger_.reset();
        impl_->storage_.reset();
        return false;
    }

    impl_->running_ = true;
    log_consensus_event(
        ConsensusLogLevel::Info,
        "nuraft_backend",
        "started",
        "node=" + std::to_string(impl_->node_id_) + " port=" + std::to_string(port));
    return true;
}

void Backend::stop() {
    if (!impl_->running_) {
        return;
    }
    log_consensus_event(ConsensusLogLevel::Debug, "nuraft_backend", "stop_begin", "node=" + std::to_string(impl_->node_id_));
    impl_->launcher_.shutdown(3);
    impl_->raft_server_.reset();
    impl_->state_machine_.reset();
    impl_->state_manager_.reset();
    impl_->logger_.reset();
    impl_->storage_.reset();
    impl_->running_ = false;
    log_consensus_event(ConsensusLogLevel::Debug, "nuraft_backend", "stopped", "node=" + std::to_string(impl_->node_id_));
}

bool Backend::is_running() const noexcept {
    return impl_->running_ && impl_->raft_server_ != nullptr;
}

bool Backend::is_leader() const noexcept {
    return is_running() && impl_->raft_server_->is_leader();
}

std::int32_t Backend::leader_id() const noexcept {
    return is_running() ? impl_->raft_server_->get_leader() : -1;
}

std::uint64_t Backend::term() const noexcept {
    return is_running() ? impl_->raft_server_->get_term() : 0;
}

std::uint64_t Backend::committed_index() const noexcept {
    return is_running() ? impl_->raft_server_->get_committed_log_idx() : 0;
}

std::uint64_t Backend::last_log_index() const noexcept {
    return is_running() ? impl_->raft_server_->get_last_log_idx() : 0;
}

std::size_t Backend::committed_entry_count() const noexcept {
    return impl_->state_machine_ ? impl_->state_machine_->committed_entry_count() : 0;
}

bool Backend::trigger_election() noexcept {
    if (!is_running()) {
        log_consensus_event(
            ConsensusLogLevel::Warning,
            "nuraft_backend",
            "trigger_election_rejected_not_running",
            "node=" + std::to_string(impl_->node_id_));
        return false;
    }
    impl_->raft_server_->restart_election_timer();
    log_consensus_event(
        ConsensusLogLevel::Debug,
        "nuraft_backend",
        "trigger_election",
        "node=" + std::to_string(impl_->node_id_) + " term=" + std::to_string(impl_->raft_server_->get_term()));
    return true;
}

std::optional<std::uint64_t> Backend::append_payload(const std::string_view payload) {
    if (!is_running()) {
        log_consensus_event(
            ConsensusLogLevel::Warning,
            "nuraft_backend",
            "append_payload_rejected_not_running",
            "node=" + std::to_string(impl_->node_id_));
        return std::nullopt;
    }
    if (!impl_->raft_server_->is_leader()) {
        log_consensus_event(
            ConsensusLogLevel::Debug,
            "nuraft_backend",
            "append_payload_rejected_not_leader",
            "node=" + std::to_string(impl_->node_id_) + " leader=" + std::to_string(impl_->raft_server_->get_leader()));
        return std::nullopt;
    }
    if (payload.empty()) {
        log_consensus_event(
            ConsensusLogLevel::Warning,
            "nuraft_backend",
            "append_payload_rejected_empty_payload",
            "node=" + std::to_string(impl_->node_id_));
        return std::nullopt;
    }

    auto data = nuraft::buffer::alloc(payload.size());
    data->put_raw(reinterpret_cast<const nuraft::byte*>(payload.data()), payload.size());
    data->pos(0);

    std::vector<nuraft::ptr<nuraft::buffer>> logs;
    logs.push_back(data);
    auto result = impl_->raft_server_->append_entries(logs);
    if (!result || result->get_result_code() != nuraft::cmd_result_code::OK || !result->get_accepted()) {
        const auto code = result ? static_cast<int>(result->get_result_code()) : -1;
        const auto accepted = result ? result->get_accepted() : false;
        log_consensus_event(
            ConsensusLogLevel::Warning,
            "nuraft_backend",
            "append_payload_failed",
            "node=" + std::to_string(impl_->node_id_) + " result_code=" + std::to_string(code) + " accepted=" +
                std::string(accepted ? "1" : "0") + " payload_bytes=" + std::to_string(payload.size()));
        return std::nullopt;
    }
    const auto index = impl_->raft_server_->get_last_log_idx();
    log_consensus_event(
        ConsensusLogLevel::Debug,
        "nuraft_backend",
        "append_payload_accepted",
        "node=" + std::to_string(impl_->node_id_) + " index=" + std::to_string(index) + " payload_bytes=" +
            std::to_string(payload.size()));
    return index;
}

std::string endpoint_for(const std::uint32_t node_id, const std::uint16_t port_base) {
    return "127.0.0.1:" + std::to_string(static_cast<std::uint32_t>(port_base) + node_id);
}

} // namespace tightrope::sync::consensus::nuraft_backend
