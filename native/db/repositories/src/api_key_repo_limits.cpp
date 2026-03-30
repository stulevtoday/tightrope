#include "api_key_repo.h"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>
#include <SQLiteCpp/Transaction.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "api_key_repo_internal.h"
#include "api_keys/limit_enforcer.h"
#include "sqlite_repo_utils.h"

namespace tightrope::db {

std::optional<std::vector<ApiKeyLimitRecord>>
replace_api_key_limits(sqlite3* db, std::string_view key_id, const std::vector<ApiKeyLimitInput>& limits) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_api_key_schema(db)) {
        return std::nullopt;
    }

    const auto api_key_pk = api_key_repo_internal::find_api_key_pk(*handle.db, key_id);
    if (!api_key_pk.has_value()) {
        return std::nullopt;
    }

    for (const auto& limit : limits) {
        if (!auth::api_keys::is_supported_limit_type(limit.limit_type) ||
            !auth::api_keys::window_to_period_seconds(limit.limit_window).has_value() || limit.max_value <= 0.0) {
            return std::nullopt;
        }
    }

    constexpr const char* kDeleteSql = "DELETE FROM api_key_limits WHERE api_key_id = ?1;";
    constexpr const char* kInsertSql = R"SQL(
INSERT INTO api_key_limits(api_key_id, limit_type, limit_value, period_seconds, limit_window, max_value, current_value, model_filter)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);
)SQL";

    try {
        SQLite::Transaction txn(*handle.db);

        SQLite::Statement delete_stmt(*handle.db, kDeleteSql);
        delete_stmt.bind(1, *api_key_pk);
        (void)delete_stmt.exec();

        SQLite::Statement insert_stmt(*handle.db, kInsertSql);
        for (const auto& limit : limits) {
            const auto period = auth::api_keys::window_to_period_seconds(limit.limit_window).value_or(604800);
            insert_stmt.reset();
            insert_stmt.clearBindings();
            insert_stmt.bind(1, *api_key_pk);
            insert_stmt.bind(2, limit.limit_type);
            insert_stmt.bind(3, limit.max_value);
            insert_stmt.bind(4, period);
            insert_stmt.bind(5, limit.limit_window);
            insert_stmt.bind(6, limit.max_value);
            insert_stmt.bind(7, limit.current_value);
            if (!api_key_repo_internal::bind_optional_text(insert_stmt, 8, limit.model_filter)) {
                return std::nullopt;
            }
            (void)insert_stmt.exec();
        }

        txn.commit();
    } catch (...) {
        return std::nullopt;
    }

    return api_key_repo_internal::load_limits(*handle.db, *api_key_pk);
}

} // namespace tightrope::db

