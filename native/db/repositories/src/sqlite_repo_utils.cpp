#include "sqlite_repo_utils.h"

#include <SQLiteCpp/Column.h>
#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Exception.h>
#include <SQLiteCpp/Statement.h>

#include <string>

#include "connection/sqlite_registry.h"

namespace tightrope::db::sqlite_repo_utils {

namespace {

const int kOpenFlags = SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE | SQLite::OPEN_FULLMUTEX;

std::optional<std::string> main_db_filename(sqlite3* db) {
    if (db == nullptr) {
        return std::nullopt;
    }

    const auto* filename = sqlite3_db_filename(db, "main");
    if (filename == nullptr || filename[0] == '\0') {
        return std::nullopt;
    }

    return std::string(filename);
}

} // namespace

DatabaseHandle resolve_database(sqlite3* db) noexcept {
    DatabaseHandle handle;
    if (db == nullptr) {
        return handle;
    }

    if (auto* mapped = connection::lookup_database(db); mapped != nullptr) {
        handle.db = mapped;
        return handle;
    }

    const auto filename = main_db_filename(db);
    if (!filename.has_value()) {
        return handle;
    }

    try {
        handle.owned_db = std::make_unique<SQLite::Database>(*filename, kOpenFlags);
        handle.db = handle.owned_db.get();
    } catch (...) {
        handle.db = nullptr;
        handle.owned_db.reset();
    }
    return handle;
}

bool exec_sql(sqlite3* db, const char* sql) noexcept {
    auto handle = resolve_database(db);
    if (!handle.valid()) {
        return false;
    }
    return exec_sql(*handle.db, sql);
}

bool exec_sql(SQLite::Database& db, const char* sql) noexcept {
    if (sql == nullptr || sql[0] == '\0') {
        return false;
    }
    try {
        (void)db.exec(sql);
        return true;
    } catch (...) {
        return false;
    }
}

bool has_column(sqlite3* db, std::string_view table, std::string_view column) noexcept {
    auto handle = resolve_database(db);
    if (!handle.valid()) {
        return false;
    }
    return has_column(*handle.db, table, column);
}

bool has_column(SQLite::Database& db, std::string_view table, std::string_view column) noexcept {
    if (table.empty() || column.empty()) {
        return false;
    }

    const std::string sql = "PRAGMA table_info(" + std::string(table) + ");";
    try {
        SQLite::Statement stmt(db, sql.c_str());
        while (stmt.executeStep()) {
            if (!stmt.getColumn(1).isNull() && stmt.getColumn(1).getString() == column) {
                return true;
            }
        }
    } catch (...) {
        return false;
    }
    return false;
}

bool ensure_column(
    sqlite3* db,
    std::string_view table,
    std::string_view column,
    const char* alter_sql
) noexcept {
    auto handle = resolve_database(db);
    if (!handle.valid()) {
        return false;
    }
    return ensure_column(*handle.db, table, column, alter_sql);
}

bool ensure_column(
    SQLite::Database& db,
    std::string_view table,
    std::string_view column,
    const char* alter_sql
) noexcept {
    if (has_column(db, table, column)) {
        return true;
    }
    return exec_sql(db, alter_sql);
}

std::optional<std::string> optional_text(const SQLite::Column& column) {
    if (column.isNull()) {
        return std::nullopt;
    }
    return column.getString();
}

std::optional<std::string> optional_text(sqlite3_stmt* stmt, const int index) {
    if (stmt == nullptr) {
        return std::nullopt;
    }
    const auto* text = sqlite3_column_text(stmt, index);
    if (text == nullptr) {
        return std::nullopt;
    }
    return std::string(reinterpret_cast<const char*>(text));
}

std::optional<std::int64_t> optional_i64(const SQLite::Column& column) {
    if (column.isNull()) {
        return std::nullopt;
    }
    return column.getInt64();
}

std::optional<std::int64_t> optional_i64(sqlite3_stmt* stmt, const int index) {
    if (stmt == nullptr || sqlite3_column_type(stmt, index) == SQLITE_NULL) {
        return std::nullopt;
    }
    return sqlite3_column_int64(stmt, index);
}

bool bind_optional_text(SQLite::Statement& stmt, int index, const std::optional<std::string>& value) noexcept {
    try {
        if (!value.has_value()) {
            stmt.bind(index);
        } else {
            stmt.bind(index, *value);
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool bind_optional_text(sqlite3_stmt* stmt, const int index, const std::optional<std::string>& value) noexcept {
    if (stmt == nullptr) {
        return false;
    }
    if (!value.has_value()) {
        return sqlite3_bind_null(stmt, index) == SQLITE_OK;
    }
    return sqlite3_bind_text(stmt, index, value->c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

} // namespace tightrope::db::sqlite_repo_utils
