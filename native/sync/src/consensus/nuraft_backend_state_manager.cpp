#include "consensus/internal/nuraft_backend_components.h"

namespace tightrope::sync::consensus::nuraft_backend::internal {

SqliteLogStore::SqliteLogStore(std::shared_ptr<SqliteRaftStorage> storage) : storage_(std::move(storage)) {}

nuraft::ulong SqliteLogStore::next_slot() const {
    return storage_->next_slot();
}

nuraft::ulong SqliteLogStore::start_index() const {
    return storage_->start_index();
}

nuraft::ptr<nuraft::log_entry> SqliteLogStore::last_entry() const {
    return storage_->last_entry();
}

nuraft::ulong SqliteLogStore::append(nuraft::ptr<nuraft::log_entry>& entry) {
    return storage_->append(entry);
}

void SqliteLogStore::write_at(const nuraft::ulong index, nuraft::ptr<nuraft::log_entry>& entry) {
    storage_->write_at(index, entry);
}

nuraft::ptr<std::vector<nuraft::ptr<nuraft::log_entry>>> SqliteLogStore::log_entries(
    const nuraft::ulong start,
    const nuraft::ulong end
) {
    return storage_->log_entries(start, end, 0);
}

nuraft::ptr<std::vector<nuraft::ptr<nuraft::log_entry>>> SqliteLogStore::log_entries_ext(
    const nuraft::ulong start,
    const nuraft::ulong end,
    const nuraft::int64 batch_size_hint_in_bytes
) {
    return storage_->log_entries(start, end, batch_size_hint_in_bytes);
}

nuraft::ptr<nuraft::log_entry> SqliteLogStore::entry_at(const nuraft::ulong index) {
    return storage_->entry_at(index);
}

nuraft::ulong SqliteLogStore::term_at(const nuraft::ulong index) {
    return storage_->term_at(index);
}

nuraft::ptr<nuraft::buffer> SqliteLogStore::pack(const nuraft::ulong index, const nuraft::int32 cnt) {
    const auto logs = storage_->log_entries(index, index + static_cast<nuraft::ulong>(cnt), 0);
    if (!logs || static_cast<nuraft::int32>(logs->size()) != cnt) {
        return nullptr;
    }

    std::vector<nuraft::ptr<nuraft::buffer>> encoded;
    encoded.reserve(static_cast<std::size_t>(cnt));
    std::size_t total_size = 0;
    for (const auto& entry : *logs) {
        auto serialized = entry->serialize();
        total_size += serialized->size();
        encoded.push_back(serialized);
    }

    auto out = nuraft::buffer::alloc(sizeof(nuraft::int32) + sizeof(nuraft::int32) * cnt + total_size);
    out->pos(0);
    out->put(cnt);
    for (const auto& serialized : encoded) {
        out->put(static_cast<nuraft::int32>(serialized->size()));
        out->put(*serialized);
    }
    out->pos(0);
    return out;
}

void SqliteLogStore::apply_pack(const nuraft::ulong index, nuraft::buffer& pack) {
    storage_->apply_pack(index, pack);
}

bool SqliteLogStore::compact(const nuraft::ulong last_log_index) {
    return storage_->compact(last_log_index);
}

bool SqliteLogStore::flush() {
    return storage_->flush();
}

SqliteStateManager::SqliteStateManager(
    const std::uint32_t node_id,
    const nuraft::ptr<nuraft::cluster_config>& config,
    const nuraft::ptr<nuraft::log_store>& log_store,
    std::shared_ptr<SqliteRaftStorage> storage
)
    : node_id_(node_id),
      initial_config_(clone_cluster_config(*config)),
      log_store_(log_store),
      storage_(std::move(storage)) {}

nuraft::ptr<nuraft::cluster_config> SqliteStateManager::load_config() {
    const auto blob = storage_->read_meta_blob(kMetaClusterConfig);
    if (blob.has_value()) {
        auto loaded = nuraft::cluster_config::deserialize(**blob);
        if (loaded) {
            return loaded;
        }
    }
    return clone_cluster_config(*initial_config_);
}

void SqliteStateManager::save_config(const nuraft::cluster_config& config) {
    auto serialized = config.serialize();
    if (serialized) {
        (void)storage_->write_meta_blob(kMetaClusterConfig, *serialized);
    }
}

void SqliteStateManager::save_state(const nuraft::srv_state& state) {
    auto serialized = state.serialize();
    if (serialized) {
        (void)storage_->write_meta_blob(kMetaServerState, *serialized);
    }
}

nuraft::ptr<nuraft::srv_state> SqliteStateManager::read_state() {
    const auto blob = storage_->read_meta_blob(kMetaServerState);
    return blob.has_value() ? nuraft::srv_state::deserialize(**blob) : nullptr;
}

nuraft::ptr<nuraft::log_store> SqliteStateManager::load_log_store() {
    return log_store_;
}

nuraft::int32 SqliteStateManager::server_id() {
    return static_cast<nuraft::int32>(node_id_);
}

void SqliteStateManager::system_exit(const int exit_code) {
    exit_code_ = exit_code;
}

} // namespace tightrope::sync::consensus::nuraft_backend::internal
