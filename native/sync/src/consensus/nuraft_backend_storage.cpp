#include "consensus/internal/nuraft_backend_components.h"
#include "consensus/internal/nuraft_backend_sqlite_statement.h"

#include <sqlite3.h>

namespace tightrope::sync::consensus::nuraft_backend::internal {

SqliteRaftStorage::SqliteRaftStorage(std::string path) : path_(std::move(path)) {}

SqliteRaftStorage::~SqliteRaftStorage() {
    close();
}

bool SqliteRaftStorage::open() {
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_.clear();
    if (db_ != nullptr) {
        return true;
    }

    const int rc = sqlite3_open_v2(
        path_.c_str(),
        &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr
    );
    if (rc != SQLITE_OK || db_ == nullptr) {
        if (db_ != nullptr) {
            const char* message = sqlite3_errmsg(db_);
            last_error_ = message != nullptr ? message : "sqlite3_open_v2 failed";
        } else {
            last_error_ = sqlite3_errstr(rc);
        }
        close_locked();
        return false;
    }

    sqlite3_busy_timeout(db_, 2000);
    if (!exec_locked("PRAGMA journal_mode=WAL;") || !exec_locked("PRAGMA synchronous=FULL;") ||
        !exec_locked("PRAGMA foreign_keys=ON;") || !ensure_schema_locked()) {
        close_locked();
        return false;
    }

    auto start_idx = read_meta_int_locked(kMetaLogStartIndex);
    if (!start_idx.has_value() || *start_idx < 1) {
        if (!write_meta_int_locked(kMetaLogStartIndex, 1)) {
            close_locked();
            return false;
        }
    }
    last_error_.clear();
    return true;
}

void SqliteRaftStorage::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    close_locked();
}

std::string SqliteRaftStorage::last_error() {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

nuraft::ulong SqliteRaftStorage::next_slot() {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto max_index = read_max_log_index_locked();
    return max_index.has_value() ? *max_index + 1 : start_index_locked();
}

nuraft::ulong SqliteRaftStorage::start_index() {
    std::lock_guard<std::mutex> lock(mutex_);
    return start_index_locked();
}

nuraft::ptr<nuraft::log_entry> SqliteRaftStorage::last_entry() {
    std::lock_guard<std::mutex> lock(mutex_);
    SqliteStatement stmt(db_, "SELECT payload FROM raft_log ORDER BY idx DESC LIMIT 1;");
    if (!stmt.ok() || sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return make_dummy_entry();
    }
    const auto* blob = sqlite3_column_blob(stmt.get(), 0);
    const auto size = sqlite3_column_bytes(stmt.get(), 0);
    auto serialized = copy_blob_to_buffer(blob, size);
    return serialized ? nuraft::log_entry::deserialize(*serialized) : make_dummy_entry();
}

nuraft::ulong SqliteRaftStorage::append(nuraft::ptr<nuraft::log_entry> entry) {
    if (!entry) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto index = next_slot_locked();
    if (!upsert_log_locked(index, *entry)) {
        return 0;
    }
    return index;
}

void SqliteRaftStorage::write_at(const nuraft::ulong index, nuraft::ptr<nuraft::log_entry> entry) {
    if (!entry) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!exec_locked("BEGIN IMMEDIATE;")) {
        return;
    }

    SqliteStatement remove_stmt(db_, "DELETE FROM raft_log WHERE idx >= ?1;");
    if (!remove_stmt.ok() || sqlite3_bind_int64(remove_stmt.get(), 1, static_cast<sqlite3_int64>(index)) != SQLITE_OK ||
        sqlite3_step(remove_stmt.get()) != SQLITE_DONE || !upsert_log_locked(index, *entry)) {
        (void)exec_locked("ROLLBACK;");
        return;
    }

    const auto start = start_index_locked();
    if (index < start && !write_meta_int_locked(kMetaLogStartIndex, index)) {
        (void)exec_locked("ROLLBACK;");
        return;
    }
    if (!exec_locked("COMMIT;")) {
        (void)exec_locked("ROLLBACK;");
    }
}

nuraft::ptr<std::vector<nuraft::ptr<nuraft::log_entry>>> SqliteRaftStorage::log_entries(
    const nuraft::ulong start,
    const nuraft::ulong end,
    const nuraft::int64 batch_size_hint_in_bytes
) {
    auto out = nuraft::cs_new<std::vector<nuraft::ptr<nuraft::log_entry>>>();
    std::lock_guard<std::mutex> lock(mutex_);
    SqliteStatement stmt(db_, "SELECT idx, payload FROM raft_log WHERE idx >= ?1 AND idx < ?2 ORDER BY idx ASC;");
    if (!stmt.ok()) {
        return nullptr;
    }
    if (sqlite3_bind_int64(stmt.get(), 1, static_cast<sqlite3_int64>(start)) != SQLITE_OK ||
        sqlite3_bind_int64(stmt.get(), 2, static_cast<sqlite3_int64>(end)) != SQLITE_OK) {
        return nullptr;
    }

    nuraft::ulong expected = start;
    bool reached_size_limit = false;
    std::size_t total_size = 0;
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        const auto index = static_cast<nuraft::ulong>(sqlite3_column_int64(stmt.get(), 0));
        if (index != expected) {
            return nullptr;
        }
        const auto* blob = sqlite3_column_blob(stmt.get(), 1);
        const auto size = sqlite3_column_bytes(stmt.get(), 1);
        auto serialized = copy_blob_to_buffer(blob, size);
        if (!serialized) {
            return nullptr;
        }
        out->push_back(nuraft::log_entry::deserialize(*serialized));
        ++expected;

        if (batch_size_hint_in_bytes > 0) {
            total_size += static_cast<std::size_t>(size);
            if (total_size >= static_cast<std::size_t>(batch_size_hint_in_bytes)) {
                reached_size_limit = true;
                break;
            }
        }
    }

    if (!reached_size_limit && rc != SQLITE_DONE) {
        return nullptr;
    }
    if (!reached_size_limit && expected != end) {
        return nullptr;
    }
    return out;
}

nuraft::ptr<nuraft::log_entry> SqliteRaftStorage::entry_at(const nuraft::ulong index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < start_index_locked()) {
        return make_dummy_entry();
    }

    SqliteStatement stmt(db_, "SELECT payload FROM raft_log WHERE idx = ?1;");
    if (!stmt.ok() || sqlite3_bind_int64(stmt.get(), 1, static_cast<sqlite3_int64>(index)) != SQLITE_OK) {
        return nullptr;
    }
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return nullptr;
    }
    const auto* blob = sqlite3_column_blob(stmt.get(), 0);
    const auto size = sqlite3_column_bytes(stmt.get(), 0);
    auto serialized = copy_blob_to_buffer(blob, size);
    return serialized ? nuraft::log_entry::deserialize(*serialized) : nullptr;
}

nuraft::ulong SqliteRaftStorage::term_at(const nuraft::ulong index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < start_index_locked()) {
        return 0;
    }
    SqliteStatement stmt(db_, "SELECT term FROM raft_log WHERE idx = ?1;");
    if (!stmt.ok() || sqlite3_bind_int64(stmt.get(), 1, static_cast<sqlite3_int64>(index)) != SQLITE_OK) {
        return 0;
    }
    return sqlite3_step(stmt.get()) == SQLITE_ROW ? static_cast<nuraft::ulong>(sqlite3_column_int64(stmt.get(), 0))
                                                  : 0;
}

bool SqliteRaftStorage::compact(const nuraft::ulong last_log_index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!exec_locked("BEGIN IMMEDIATE;")) {
        return false;
    }

    SqliteStatement stmt(db_, "DELETE FROM raft_log WHERE idx <= ?1;");
    if (!stmt.ok() || sqlite3_bind_int64(stmt.get(), 1, static_cast<sqlite3_int64>(last_log_index)) != SQLITE_OK ||
        sqlite3_step(stmt.get()) != SQLITE_DONE) {
        (void)exec_locked("ROLLBACK;");
        return false;
    }

    const auto start = start_index_locked();
    const auto next_start = start <= last_log_index ? last_log_index + 1 : start;
    if (!write_meta_int_locked(kMetaLogStartIndex, next_start)) {
        (void)exec_locked("ROLLBACK;");
        return false;
    }
    if (!exec_locked("COMMIT;")) {
        (void)exec_locked("ROLLBACK;");
        return false;
    }
    return true;
}

bool SqliteRaftStorage::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    return exec_locked("PRAGMA wal_checkpoint(TRUNCATE);");
}

void SqliteRaftStorage::apply_pack(const nuraft::ulong index, nuraft::buffer& pack) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!exec_locked("BEGIN IMMEDIATE;")) {
        return;
    }

    pack.pos(0);
    const auto count = pack.get_int();
    bool ok = true;
    for (nuraft::int32 offset = 0; offset < count; ++offset) {
        const auto size = pack.get_int();
        auto raw = nuraft::buffer::alloc(static_cast<std::size_t>(size));
        pack.get(raw);
        auto entry = nuraft::log_entry::deserialize(*raw);
        const auto current = index + static_cast<nuraft::ulong>(offset);
        if (!upsert_log_locked(current, *entry)) {
            ok = false;
            break;
        }
    }

    if (ok) {
        const auto min_index = read_min_log_index_locked();
        const auto start = min_index.has_value() && *min_index > 0 ? *min_index : 1;
        ok = write_meta_int_locked(kMetaLogStartIndex, start);
    }

    if (!ok || !exec_locked("COMMIT;")) {
        (void)exec_locked("ROLLBACK;");
    }
}

std::optional<nuraft::ptr<nuraft::buffer>> SqliteRaftStorage::read_meta_blob(const std::string_view key) {
    std::lock_guard<std::mutex> lock(mutex_);
    return read_meta_blob_locked(key);
}

bool SqliteRaftStorage::write_meta_blob(const std::string_view key, nuraft::buffer& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    return write_meta_blob_locked(key, value);
}

bool SqliteRaftStorage::ensure_schema_locked() {
    return exec_locked(
               "CREATE TABLE IF NOT EXISTS raft_meta ("
               "  key TEXT PRIMARY KEY,"
               "  value_int INTEGER,"
               "  value_blob BLOB"
               ");"
           ) &&
           exec_locked(
               "CREATE TABLE IF NOT EXISTS raft_log ("
               "  idx INTEGER PRIMARY KEY,"
               "  term INTEGER NOT NULL,"
               "  payload BLOB NOT NULL"
               ");"
           ) &&
           exec_locked(
               "CREATE TABLE IF NOT EXISTS raft_committed ("
               "  log_idx INTEGER PRIMARY KEY,"
               "  payload BLOB NOT NULL"
               ");"
           );
}

bool SqliteRaftStorage::append_committed(const nuraft::ulong log_idx, const void* data, const std::size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    SqliteStatement stmt(db_,
        "INSERT OR REPLACE INTO raft_committed(log_idx, payload) VALUES (?1, ?2);");
    if (!stmt.ok() ||
        sqlite3_bind_int64(stmt.get(), 1, static_cast<sqlite3_int64>(log_idx)) != SQLITE_OK ||
        sqlite3_bind_blob(stmt.get(), 2, data, static_cast<int>(size), SQLITE_TRANSIENT) != SQLITE_OK) {
        return false;
    }
    return sqlite3_step(stmt.get()) == SQLITE_DONE;
}

std::size_t SqliteRaftStorage::committed_count() {
    std::lock_guard<std::mutex> lock(mutex_);
    SqliteStatement stmt(db_, "SELECT COUNT(*) FROM raft_committed;");
    if (!stmt.ok() || sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return 0;
    }
    return static_cast<std::size_t>(sqlite3_column_int64(stmt.get(), 0));
}

std::vector<std::string> SqliteRaftStorage::load_all_committed() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> payloads;
    SqliteStatement stmt(db_, "SELECT payload FROM raft_committed ORDER BY log_idx ASC;");
    if (!stmt.ok()) {
        return payloads;
    }
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        const auto* blob = sqlite3_column_blob(stmt.get(), 0);
        const auto size = sqlite3_column_bytes(stmt.get(), 0);
        if (blob != nullptr && size > 0) {
            payloads.emplace_back(reinterpret_cast<const char*>(blob), static_cast<std::size_t>(size));
        }
    }
    return payloads;
}

std::optional<nuraft::ulong> SqliteRaftStorage::max_committed_index() {
    std::lock_guard<std::mutex> lock(mutex_);
    SqliteStatement stmt(db_, "SELECT MAX(log_idx) FROM raft_committed;");
    if (!stmt.ok() || sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }
    if (sqlite3_column_type(stmt.get(), 0) == SQLITE_NULL) {
        return std::nullopt;
    }
    return static_cast<nuraft::ulong>(sqlite3_column_int64(stmt.get(), 0));
}

bool SqliteRaftStorage::exec_locked(const char* sql) {
    char* error = nullptr;
    const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &error);
    if (rc != SQLITE_OK) {
        if (error != nullptr) {
            last_error_ = error;
        } else if (db_ != nullptr) {
            const char* message = sqlite3_errmsg(db_);
            last_error_ = message != nullptr ? message : "sqlite3_exec failed";
        } else {
            last_error_ = "sqlite3_exec failed";
        }
    } else {
        last_error_.clear();
    }
    if (error != nullptr) {
        sqlite3_free(error);
    }
    return rc == SQLITE_OK;
}

void SqliteRaftStorage::close_locked() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

} // namespace tightrope::sync::consensus::nuraft_backend::internal
