#include "api_key_repo_internal.h"

#include <SQLiteCpp/Column.h>
#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "api_keys/limit_enforcer.h"
#include "sqlite_repo_utils.h"

namespace tightrope::db::api_key_repo_internal {

bool exec_sql(sqlite3* db, const char* sql) noexcept {
    return sqlite_repo_utils::exec_sql(db, sql);
}

bool exec_sql(SQLite::Database& db, const char* sql) noexcept {
    return sqlite_repo_utils::exec_sql(db, sql);
}

bool has_column(sqlite3* db, std::string_view table, std::string_view column) noexcept {
    return sqlite_repo_utils::has_column(db, table, column);
}

bool has_column(SQLite::Database& db, std::string_view table, std::string_view column) noexcept {
    return sqlite_repo_utils::has_column(db, table, column);
}

bool ensure_column(
    sqlite3* db,
    std::string_view table,
    std::string_view column,
    const char* alter_sql
) noexcept {
    return sqlite_repo_utils::ensure_column(db, table, column, alter_sql);
}

bool ensure_column(
    SQLite::Database& db,
    std::string_view table,
    std::string_view column,
    const char* alter_sql
) noexcept {
    return sqlite_repo_utils::ensure_column(db, table, column, alter_sql);
}

bool bind_optional_text(SQLite::Statement& stmt, const int index, const std::optional<std::string>& value) noexcept {
    return sqlite_repo_utils::bind_optional_text(stmt, index, value);
}

std::optional<std::int64_t> find_api_key_pk(sqlite3* db, std::string_view key_id) {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid()) {
        return std::nullopt;
    }
    return find_api_key_pk(*handle.db, key_id);
}

std::optional<std::int64_t> find_api_key_pk(SQLite::Database& db, std::string_view key_id) {
    constexpr const char* kSql = "SELECT id FROM api_keys WHERE key_id = ?1 LIMIT 1;";
    try {
        SQLite::Statement stmt(db, kSql);
        stmt.bind(1, std::string(key_id));
        if (!stmt.executeStep()) {
            return std::nullopt;
        }
        return stmt.getColumn(0).getInt64();
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<ApiKeyLimitRecord> load_limits(sqlite3* db, const std::int64_t api_key_pk) {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid()) {
        return {};
    }
    return load_limits(*handle.db, api_key_pk);
}

std::vector<ApiKeyLimitRecord> load_limits(SQLite::Database& db, const std::int64_t api_key_pk) {
    std::vector<ApiKeyLimitRecord> records;
    constexpr const char* kSql = R"SQL(
SELECT id, api_key_id, limit_type, limit_window, max_value, current_value, model_filter, limit_value, period_seconds
FROM api_key_limits
WHERE api_key_id = ?1
ORDER BY id ASC;
)SQL";

    try {
        SQLite::Statement stmt(db, kSql);
        stmt.bind(1, api_key_pk);
        while (stmt.executeStep()) {
            ApiKeyLimitRecord record;
            record.id = stmt.getColumn(0).getInt64();
            record.api_key_id = stmt.getColumn(1).getInt64();
            if (const auto value = sqlite_repo_utils::optional_text(stmt.getColumn(2)); value.has_value()) {
                record.limit_type = *value;
            }
            if (const auto value = sqlite_repo_utils::optional_text(stmt.getColumn(3)); value.has_value()) {
                record.limit_window = *value;
            }

            if (!stmt.getColumn(4).isNull()) {
                record.max_value = stmt.getColumn(4).getDouble();
            } else if (!stmt.getColumn(7).isNull()) {
                record.max_value = stmt.getColumn(7).getDouble();
            }
            if (!stmt.getColumn(5).isNull()) {
                record.current_value = stmt.getColumn(5).getDouble();
            }

            record.model_filter = sqlite_repo_utils::optional_text(stmt.getColumn(6));
            if (record.limit_window.empty()) {
                const auto maybe_window = auth::api_keys::period_seconds_to_window(stmt.getColumn(8).getInt64());
                record.limit_window = maybe_window.value_or("weekly");
            }
            records.push_back(std::move(record));
        }
    } catch (...) {
        return {};
    }
    return records;
}

std::optional<ApiKeyRecord> load_key_by_clause(sqlite3* db, const char* clause, std::string_view value) {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid()) {
        return std::nullopt;
    }
    return load_key_by_clause(*handle.db, clause, value);
}

std::optional<ApiKeyRecord> load_key_by_clause(SQLite::Database& db, const char* clause, std::string_view value) {
    const std::string sql = std::string(
                                "SELECT id, key_id, key_hash, key_prefix, name, is_active, allowed_models, "
                                "enforced_model, enforced_reasoning_effort, expires_at, created_at, last_used_at "
                                "FROM api_keys WHERE "
                            ) +
                            clause + " LIMIT 1;";

    try {
        SQLite::Statement stmt(db, sql.c_str());
        stmt.bind(1, std::string(value));
        if (!stmt.executeStep()) {
            return std::nullopt;
        }

        ApiKeyRecord record;
        record.id = stmt.getColumn(0).getInt64();
        if (const auto text = sqlite_repo_utils::optional_text(stmt.getColumn(1)); text.has_value()) {
            record.key_id = *text;
        }
        if (const auto text = sqlite_repo_utils::optional_text(stmt.getColumn(2)); text.has_value()) {
            record.key_hash = *text;
        }
        if (const auto text = sqlite_repo_utils::optional_text(stmt.getColumn(3)); text.has_value()) {
            record.key_prefix = *text;
        }
        if (const auto text = sqlite_repo_utils::optional_text(stmt.getColumn(4)); text.has_value()) {
            record.name = *text;
        }
        record.is_active = !stmt.getColumn(5).isNull() && stmt.getColumn(5).getInt() != 0;
        record.allowed_models = sqlite_repo_utils::optional_text(stmt.getColumn(6));
        record.enforced_model = sqlite_repo_utils::optional_text(stmt.getColumn(7));
        record.enforced_reasoning_effort = sqlite_repo_utils::optional_text(stmt.getColumn(8));
        record.expires_at = sqlite_repo_utils::optional_text(stmt.getColumn(9));
        if (const auto text = sqlite_repo_utils::optional_text(stmt.getColumn(10)); text.has_value()) {
            record.created_at = *text;
        }
        record.last_used_at = sqlite_repo_utils::optional_text(stmt.getColumn(11));
        record.limits = load_limits(db, record.id);
        return record;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace tightrope::db::api_key_repo_internal

