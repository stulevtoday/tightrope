#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <libnuraft/nuraft.hxx>

struct sqlite3;

namespace tightrope::sync::consensus::nuraft_backend::internal {

inline constexpr std::string_view kMetaLogStartIndex = "log_start_index";
inline constexpr std::string_view kMetaClusterConfig = "cluster_config";
inline constexpr std::string_view kMetaServerState = "server_state";

class SqliteRaftStorage;

std::vector<std::uint32_t> normalize_members(std::vector<std::uint32_t> members);
nuraft::ptr<nuraft::srv_config> clone_srv_config(const nuraft::srv_config& config);
nuraft::ptr<nuraft::cluster_config> clone_cluster_config(const nuraft::cluster_config& config);
nuraft::ptr<nuraft::cluster_config> build_cluster_config(
    const std::vector<std::uint32_t>& members,
    std::uint16_t port_base
);
std::string make_storage_path(const std::string& base_dir, std::uint32_t node_id, std::uint16_t port_base);
nuraft::ptr<nuraft::buffer> copy_blob_to_buffer(const void* blob, int size);
nuraft::ptr<nuraft::log_entry> make_dummy_entry();

class NoopLogger final : public nuraft::logger {
public:
    void put_details(int, const char*, const char*, std::size_t, const std::string&) override;
};

class InMemoryStateMachine final : public nuraft::state_machine {
public:
    InMemoryStateMachine(const nuraft::ptr<nuraft::cluster_config>& config,
                         std::shared_ptr<SqliteRaftStorage> storage);
    explicit InMemoryStateMachine(const nuraft::ptr<nuraft::cluster_config>& config);

    nuraft::ptr<nuraft::buffer> commit(nuraft::ulong log_idx, nuraft::buffer& data) override;
    bool apply_snapshot(nuraft::snapshot& snapshot) override;
    nuraft::ptr<nuraft::snapshot> last_snapshot() override;
    nuraft::ulong last_commit_index() override;
    void create_snapshot(nuraft::snapshot& snapshot, nuraft::async_result<bool>::handler_type& when_done) override;
    std::size_t committed_entry_count() const;

private:
    mutable std::mutex mutex_;
    nuraft::ptr<nuraft::cluster_config> config_;
    nuraft::ptr<nuraft::snapshot> snapshot_;
    std::atomic<nuraft::ulong> commit_index_{0};
    std::vector<std::string> committed_payloads_;
    std::shared_ptr<SqliteRaftStorage> storage_;
};

class SqliteRaftStorage {
public:
    explicit SqliteRaftStorage(std::string path);
    ~SqliteRaftStorage();

    bool open();
    void close();
    std::string last_error();

    nuraft::ulong next_slot();
    nuraft::ulong start_index();
    nuraft::ptr<nuraft::log_entry> last_entry();
    nuraft::ulong append(nuraft::ptr<nuraft::log_entry> entry);
    void write_at(nuraft::ulong index, nuraft::ptr<nuraft::log_entry> entry);
    nuraft::ptr<std::vector<nuraft::ptr<nuraft::log_entry>>> log_entries(
        nuraft::ulong start,
        nuraft::ulong end,
        nuraft::int64 batch_size_hint_in_bytes
    );
    nuraft::ptr<nuraft::log_entry> entry_at(nuraft::ulong index);
    nuraft::ulong term_at(nuraft::ulong index);
    bool compact(nuraft::ulong last_log_index);
    bool flush();
    void apply_pack(nuraft::ulong index, nuraft::buffer& pack);
    std::optional<nuraft::ptr<nuraft::buffer>> read_meta_blob(std::string_view key);
    bool write_meta_blob(std::string_view key, nuraft::buffer& value);

    bool append_committed(nuraft::ulong log_idx, const void* data, std::size_t size);
    std::size_t committed_count();
    std::vector<std::string> load_all_committed();
    std::optional<nuraft::ulong> max_committed_index();

private:
    bool ensure_schema_locked();
    bool exec_locked(const char* sql);
    void close_locked();
    std::optional<nuraft::ulong> read_meta_int_locked(std::string_view key);
    bool write_meta_int_locked(std::string_view key, nuraft::ulong value);
    std::optional<nuraft::ptr<nuraft::buffer>> read_meta_blob_locked(std::string_view key);
    bool write_meta_blob_locked(std::string_view key, nuraft::buffer& value);
    std::optional<nuraft::ulong> read_max_log_index_locked();
    std::optional<nuraft::ulong> read_min_log_index_locked();
    nuraft::ulong start_index_locked();
    nuraft::ulong next_slot_locked();
    bool upsert_log_locked(nuraft::ulong index, nuraft::log_entry& entry);

    std::string path_;
    std::string last_error_;
    sqlite3* db_ = nullptr;
    std::mutex mutex_;
};

class SqliteLogStore final : public nuraft::log_store {
public:
    explicit SqliteLogStore(std::shared_ptr<SqliteRaftStorage> storage);

    nuraft::ulong next_slot() const override;
    nuraft::ulong start_index() const override;
    nuraft::ptr<nuraft::log_entry> last_entry() const override;
    nuraft::ulong append(nuraft::ptr<nuraft::log_entry>& entry) override;
    void write_at(nuraft::ulong index, nuraft::ptr<nuraft::log_entry>& entry) override;
    nuraft::ptr<std::vector<nuraft::ptr<nuraft::log_entry>>> log_entries(nuraft::ulong start, nuraft::ulong end)
        override;
    nuraft::ptr<std::vector<nuraft::ptr<nuraft::log_entry>>> log_entries_ext(
        nuraft::ulong start,
        nuraft::ulong end,
        nuraft::int64 batch_size_hint_in_bytes
    ) override;
    nuraft::ptr<nuraft::log_entry> entry_at(nuraft::ulong index) override;
    nuraft::ulong term_at(nuraft::ulong index) override;
    nuraft::ptr<nuraft::buffer> pack(nuraft::ulong index, nuraft::int32 cnt) override;
    void apply_pack(nuraft::ulong index, nuraft::buffer& pack) override;
    bool compact(nuraft::ulong last_log_index) override;
    bool flush() override;

private:
    std::shared_ptr<SqliteRaftStorage> storage_;
};

class SqliteStateManager final : public nuraft::state_mgr {
public:
    SqliteStateManager(
        std::uint32_t node_id,
        const nuraft::ptr<nuraft::cluster_config>& config,
        const nuraft::ptr<nuraft::log_store>& log_store,
        std::shared_ptr<SqliteRaftStorage> storage
    );

    nuraft::ptr<nuraft::cluster_config> load_config() override;
    void save_config(const nuraft::cluster_config& config) override;
    void save_state(const nuraft::srv_state& state) override;
    nuraft::ptr<nuraft::srv_state> read_state() override;
    nuraft::ptr<nuraft::log_store> load_log_store() override;
    nuraft::int32 server_id() override;
    void system_exit(int exit_code) override;

private:
    std::uint32_t node_id_ = 0;
    nuraft::ptr<nuraft::cluster_config> initial_config_;
    nuraft::ptr<nuraft::log_store> log_store_;
    std::shared_ptr<SqliteRaftStorage> storage_;
    int exit_code_ = 0;
};

} // namespace tightrope::sync::consensus::nuraft_backend::internal
