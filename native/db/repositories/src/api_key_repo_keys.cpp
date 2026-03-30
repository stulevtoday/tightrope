#include "api_key_repo.h"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "api_key_repo_internal.h"
#include "sqlite_repo_utils.h"

namespace tightrope::db {

std::optional<ApiKeyRecord> create_api_key(
    sqlite3* db,
    std::string_view key_id,
    std::string_view key_hash,
    std::string_view key_prefix,
    std::string_view name,
    const std::optional<std::string>& allowed_models,
    const std::optional<std::string>& enforced_model,
    const std::optional<std::string>& enforced_reasoning_effort,
    const std::optional<std::string>& expires_at
) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_api_key_schema(db) || key_id.empty() || key_hash.empty() || name.empty()) {
        return std::nullopt;
    }

    constexpr const char* kSql = R"SQL(
INSERT INTO api_keys(
    key_id, key_hash, key_prefix, name, status, is_active, allowed_models, enforced_model, enforced_reasoning_effort, expires_at
) VALUES (?1, ?2, ?3, ?4, 'active', 1, ?5, ?6, ?7, ?8);
)SQL";

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, std::string(key_id));
        stmt.bind(2, std::string(key_hash));
        stmt.bind(3, std::string(key_prefix));
        stmt.bind(4, std::string(name));
        if (!api_key_repo_internal::bind_optional_text(stmt, 5, allowed_models) ||
            !api_key_repo_internal::bind_optional_text(stmt, 6, enforced_model) ||
            !api_key_repo_internal::bind_optional_text(stmt, 7, enforced_reasoning_effort) ||
            !api_key_repo_internal::bind_optional_text(stmt, 8, expires_at)) {
            return std::nullopt;
        }
        if (stmt.exec() <= 0) {
            return std::nullopt;
        }
    } catch (...) {
        return std::nullopt;
    }

    return get_api_key_by_key_id(db, key_id);
}

std::vector<ApiKeyRecord> list_api_keys(sqlite3* db) noexcept {
    std::vector<ApiKeyRecord> records;
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_api_key_schema(db)) {
        return records;
    }

    constexpr const char* kSql = "SELECT key_id FROM api_keys ORDER BY created_at DESC, id DESC;";
    try {
        std::vector<std::string> key_ids;
        SQLite::Statement stmt(*handle.db, kSql);
        while (stmt.executeStep()) {
            if (stmt.getColumn(0).isNull()) {
                continue;
            }
            key_ids.push_back(stmt.getColumn(0).getString());
        }
        for (const auto& key_id : key_ids) {
            auto row = api_key_repo_internal::load_key_by_clause(*handle.db, "key_id = ?1", key_id);
            if (row.has_value()) {
                records.push_back(std::move(*row));
            }
        }
    } catch (...) {
        return {};
    }
    return records;
}

std::optional<ApiKeyRecord> get_api_key_by_key_id(sqlite3* db, std::string_view key_id) noexcept {
    if (!ensure_api_key_schema(db) || key_id.empty()) {
        return std::nullopt;
    }
    return api_key_repo_internal::load_key_by_clause(db, "key_id = ?1", key_id);
}

std::optional<ApiKeyRecord> get_api_key_by_hash(sqlite3* db, std::string_view key_hash) noexcept {
    if (!ensure_api_key_schema(db) || key_hash.empty()) {
        return std::nullopt;
    }
    return api_key_repo_internal::load_key_by_clause(db, "key_hash = ?1", key_hash);
}

std::optional<ApiKeyRecord> update_api_key(sqlite3* db, std::string_view key_id, const ApiKeyPatch& patch) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid()) {
        return std::nullopt;
    }

    auto current = get_api_key_by_key_id(db, key_id);
    if (!current.has_value()) {
        return std::nullopt;
    }

    const auto name = patch.name.value_or(current->name);
    const auto is_active = patch.is_active.value_or(current->is_active);
    const auto key_hash = patch.key_hash.value_or(current->key_hash);
    const auto key_prefix = patch.key_prefix.value_or(current->key_prefix);
    const auto allowed_models = patch.allowed_models.value_or(current->allowed_models.value_or(""));
    const auto enforced_model = patch.enforced_model.value_or(current->enforced_model.value_or(""));
    const auto enforced_reasoning_effort =
        patch.enforced_reasoning_effort.value_or(current->enforced_reasoning_effort.value_or(""));
    const auto expires_at = patch.expires_at.value_or(current->expires_at.value_or(""));
    const auto last_used_at = patch.last_used_at.value_or(current->last_used_at.value_or(""));

    constexpr const char* kSql = R"SQL(
UPDATE api_keys
SET
    name = ?2,
    status = CASE WHEN ?3 = 1 THEN 'active' ELSE 'inactive' END,
    is_active = ?3,
    key_hash = ?4,
    key_prefix = ?5,
    allowed_models = NULLIF(?6, ''),
    enforced_model = NULLIF(?7, ''),
    enforced_reasoning_effort = NULLIF(?8, ''),
    expires_at = NULLIF(?9, ''),
    last_used_at = NULLIF(?10, ''),
    updated_at = datetime('now')
WHERE key_id = ?1;
)SQL";

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, std::string(key_id));
        stmt.bind(2, name);
        stmt.bind(3, is_active ? 1 : 0);
        stmt.bind(4, key_hash);
        stmt.bind(5, key_prefix);
        stmt.bind(6, allowed_models);
        stmt.bind(7, enforced_model);
        stmt.bind(8, enforced_reasoning_effort);
        stmt.bind(9, expires_at);
        stmt.bind(10, last_used_at);
        (void)stmt.exec();
    } catch (...) {
        return std::nullopt;
    }

    return get_api_key_by_key_id(db, key_id);
}

bool delete_api_key(sqlite3* db, std::string_view key_id) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_api_key_schema(db) || key_id.empty()) {
        return false;
    }

    constexpr const char* kSql = "DELETE FROM api_keys WHERE key_id = ?1;";
    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, std::string(key_id));
        (void)stmt.exec();
        return handle.db->getChanges() > 0;
    } catch (...) {
        return false;
    }
}

std::optional<ApiKeyRecord>
rotate_api_key_secret(sqlite3* db, std::string_view key_id, std::string_view key_hash, std::string_view key_prefix) noexcept {
    ApiKeyPatch patch;
    patch.key_hash = std::string(key_hash);
    patch.key_prefix = std::string(key_prefix);
    return update_api_key(db, key_id, patch);
}

} // namespace tightrope::db
