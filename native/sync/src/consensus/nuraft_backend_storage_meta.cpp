#include "consensus/internal/nuraft_backend_components.h"
#include "consensus/internal/nuraft_backend_sqlite_statement.h"

#include <sqlite3.h>

namespace tightrope::sync::consensus::nuraft_backend::internal {

std::optional<nuraft::ulong> SqliteRaftStorage::read_meta_int_locked(const std::string_view key) {
    SqliteStatement stmt(db_, "SELECT value_int FROM raft_meta WHERE key = ?1;");
    if (!stmt.ok() || sqlite3_bind_text(stmt.get(), 1, key.data(), static_cast<int>(key.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
        return std::nullopt;
    }
    if (sqlite3_step(stmt.get()) != SQLITE_ROW || sqlite3_column_type(stmt.get(), 0) == SQLITE_NULL) {
        return std::nullopt;
    }
    return static_cast<nuraft::ulong>(sqlite3_column_int64(stmt.get(), 0));
}

bool SqliteRaftStorage::write_meta_int_locked(const std::string_view key, const nuraft::ulong value) {
    SqliteStatement stmt(
        db_,
        "INSERT INTO raft_meta(key, value_int, value_blob) VALUES (?1, ?2, NULL) "
        "ON CONFLICT(key) DO UPDATE SET value_int=excluded.value_int, value_blob=NULL;"
    );
    if (!stmt.ok() || sqlite3_bind_text(stmt.get(), 1, key.data(), static_cast<int>(key.size()), SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(stmt.get(), 2, static_cast<sqlite3_int64>(value)) != SQLITE_OK) {
        return false;
    }
    return sqlite3_step(stmt.get()) == SQLITE_DONE;
}

std::optional<nuraft::ptr<nuraft::buffer>> SqliteRaftStorage::read_meta_blob_locked(const std::string_view key) {
    SqliteStatement stmt(db_, "SELECT value_blob FROM raft_meta WHERE key = ?1;");
    if (!stmt.ok() || sqlite3_bind_text(stmt.get(), 1, key.data(), static_cast<int>(key.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
        return std::nullopt;
    }
    if (sqlite3_step(stmt.get()) != SQLITE_ROW || sqlite3_column_type(stmt.get(), 0) == SQLITE_NULL) {
        return std::nullopt;
    }
    const auto* blob = sqlite3_column_blob(stmt.get(), 0);
    const auto size = sqlite3_column_bytes(stmt.get(), 0);
    auto copied = copy_blob_to_buffer(blob, size);
    return copied ? std::optional<nuraft::ptr<nuraft::buffer>>(copied) : std::nullopt;
}

bool SqliteRaftStorage::write_meta_blob_locked(const std::string_view key, nuraft::buffer& value) {
    SqliteStatement stmt(
        db_,
        "INSERT INTO raft_meta(key, value_int, value_blob) VALUES (?1, NULL, ?2) "
        "ON CONFLICT(key) DO UPDATE SET value_int=NULL, value_blob=excluded.value_blob;"
    );
    if (!stmt.ok() || sqlite3_bind_text(stmt.get(), 1, key.data(), static_cast<int>(key.size()), SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_blob(stmt.get(), 2, value.data_begin(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
        return false;
    }
    return sqlite3_step(stmt.get()) == SQLITE_DONE;
}

std::optional<nuraft::ulong> SqliteRaftStorage::read_max_log_index_locked() {
    SqliteStatement stmt(db_, "SELECT MAX(idx) FROM raft_log;");
    if (!stmt.ok() || sqlite3_step(stmt.get()) != SQLITE_ROW || sqlite3_column_type(stmt.get(), 0) == SQLITE_NULL) {
        return std::nullopt;
    }
    return static_cast<nuraft::ulong>(sqlite3_column_int64(stmt.get(), 0));
}

std::optional<nuraft::ulong> SqliteRaftStorage::read_min_log_index_locked() {
    SqliteStatement stmt(db_, "SELECT MIN(idx) FROM raft_log;");
    if (!stmt.ok() || sqlite3_step(stmt.get()) != SQLITE_ROW || sqlite3_column_type(stmt.get(), 0) == SQLITE_NULL) {
        return std::nullopt;
    }
    return static_cast<nuraft::ulong>(sqlite3_column_int64(stmt.get(), 0));
}

nuraft::ulong SqliteRaftStorage::start_index_locked() {
    const auto start = read_meta_int_locked(kMetaLogStartIndex);
    return (start.has_value() && *start > 0) ? *start : 1;
}

nuraft::ulong SqliteRaftStorage::next_slot_locked() {
    const auto max_index = read_max_log_index_locked();
    return max_index.has_value() ? *max_index + 1 : start_index_locked();
}

bool SqliteRaftStorage::upsert_log_locked(const nuraft::ulong index, nuraft::log_entry& entry) {
    auto serialized = entry.serialize();
    if (!serialized) {
        return false;
    }

    SqliteStatement stmt(
        db_,
        "INSERT INTO raft_log(idx, term, payload) VALUES (?1, ?2, ?3) "
        "ON CONFLICT(idx) DO UPDATE SET term=excluded.term, payload=excluded.payload;"
    );
    if (!stmt.ok() || sqlite3_bind_int64(stmt.get(), 1, static_cast<sqlite3_int64>(index)) != SQLITE_OK ||
        sqlite3_bind_int64(stmt.get(), 2, static_cast<sqlite3_int64>(entry.get_term())) != SQLITE_OK ||
        sqlite3_bind_blob(stmt.get(), 3, serialized->data_begin(), static_cast<int>(serialized->size()), SQLITE_TRANSIENT) != SQLITE_OK) {
        return false;
    }
    return sqlite3_step(stmt.get()) == SQLITE_DONE;
}

} // namespace tightrope::sync::consensus::nuraft_backend::internal
