#include "sticky_affinity.h"

#include <array>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include <glaze/glaze.hpp>

#include "config_loader.h"
#include "text/ascii.h"
#include "connection/sqlite_pool.h"
#include "logging/logger.h"
#include "oauth/token_refresh.h"
#include "repositories/account_repo.h"
#include "repositories/session_repo.h"

namespace tightrope::proxy::session {

namespace {

using Json = glz::generic;
using JsonObject = Json::object_t;

constexpr std::int64_t kStickyTtlMs = 30 * 60 * 1000;
constexpr std::int64_t kCleanupIntervalMs = 60 * 1000;
constexpr std::string_view kAccountHeader = "chatgpt-account-id";

constexpr std::array<std::string_view, 6> kStickyKeyFields = {
    "prompt_cache_key",
    "promptCacheKey",
    "session_id",
    "sessionId",
    "thread_id",
    "threadId",
};

constexpr const char* kFindExactCredentialSql = R"SQL(
SELECT chatgpt_account_id, access_token_encrypted
FROM accounts
WHERE status = 'active'
  AND chatgpt_account_id = ?1
  AND chatgpt_account_id IS NOT NULL
  AND trim(chatgpt_account_id) != ''
  AND access_token_encrypted IS NOT NULL
  AND length(access_token_encrypted) > 0
LIMIT 1;
)SQL";

constexpr const char* kFindAnyCredentialSql = R"SQL(
SELECT chatgpt_account_id, access_token_encrypted
FROM accounts
WHERE status = 'active'
  AND chatgpt_account_id IS NOT NULL
  AND trim(chatgpt_account_id) != ''
  AND access_token_encrypted IS NOT NULL
  AND length(access_token_encrypted) > 0
ORDER BY updated_at DESC, id DESC
LIMIT 1;
)SQL";

struct StickyDbState {
    std::mutex mutex;
    std::string db_path;
    std::unique_ptr<db::SqlitePool> pool;
    bool schema_ready = false;
    std::int64_t last_cleanup_ms = 0;
};

std::int64_t now_ms() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

StickyDbState& sticky_db_state() {
    static StickyDbState state;
    return state;
}

std::string configured_db_path() {
    auto config = config::load_config();
    if (config.db_path.empty()) {
        return "store.db";
    }
    return config.db_path;
}

sqlite3* ensure_db(StickyDbState& state) {
    const auto desired_path = configured_db_path();
    if (!state.pool || state.db_path != desired_path) {
        if (state.pool) {
            state.pool->close();
        }
        state.pool = std::make_unique<db::SqlitePool>(desired_path);
        state.db_path = desired_path;
        state.schema_ready = false;
        state.last_cleanup_ms = 0;
    }

    if (!state.pool->open()) {
        return nullptr;
    }

    sqlite3* db = state.pool->connection();
    if (db == nullptr) {
        return nullptr;
    }

    if (!state.schema_ready) {
        if (!db::ensure_proxy_sticky_session_schema(db)) {
            return nullptr;
        }
        state.schema_ready = true;
    }

    return db;
}

void maybe_purge_expired(StickyDbState& state, sqlite3* db, const std::int64_t now) {
    if (now < state.last_cleanup_ms) {
        state.last_cleanup_ms = now;
        return;
    }
    if (now - state.last_cleanup_ms < kCleanupIntervalMs) {
        return;
    }
    (void)db::purge_expired_proxy_sticky_sessions(db, now);
    state.last_cleanup_ms = now;
}

std::string read_string_field(const JsonObject& object, std::string_view key) {
    const auto it = object.find(std::string(key));
    if (it == object.end() || !it->second.is_string()) {
        return {};
    }
    const auto& value = it->second.get_string();
    if (value.empty()) {
        return {};
    }
    return value;
}

std::string sticky_key_from_object(const JsonObject& object) {
    for (const auto key : kStickyKeyFields) {
        auto value = read_string_field(object, key);
        if (!value.empty()) {
            return value;
        }
    }

    const auto metadata_it = object.find("metadata");
    if (metadata_it != object.end() && metadata_it->second.is_object()) {
        const auto& metadata = metadata_it->second.get_object();
        for (const auto key : kStickyKeyFields) {
            auto value = read_string_field(metadata, key);
            if (!value.empty()) {
                return value;
            }
        }
    }

    return {};
}

std::string extract_sticky_key(const std::string& raw_request_body) {
    Json payload{};
    if (const auto ec = glz::read_json(payload, raw_request_body); ec) {
        return {};
    }
    if (!payload.is_object()) {
        return {};
    }
    return sticky_key_from_object(payload.get_object());
}

std::string account_from_headers(const openai::HeaderMap& inbound_headers) {
    for (const auto& [name, value] : inbound_headers) {
        if (core::text::equals_case_insensitive(name, kAccountHeader) && !value.empty()) {
            return value;
        }
    }
    return {};
}

std::optional<UpstreamAccountCredentials> read_credentials_row(sqlite3_stmt* stmt) {
    if (stmt == nullptr || sqlite3_step(stmt) != SQLITE_ROW) {
        return std::nullopt;
    }
    const auto* account_raw = sqlite3_column_text(stmt, 0);
    const auto* token_raw = sqlite3_column_text(stmt, 1);
    if (account_raw == nullptr || token_raw == nullptr) {
        return std::nullopt;
    }
    const auto account_id = std::string(core::text::trim_ascii(reinterpret_cast<const char*>(account_raw)));
    const auto access_token = std::string(core::text::trim_ascii(reinterpret_cast<const char*>(token_raw)));
    if (account_id.empty() || access_token.empty()) {
        return std::nullopt;
    }
    return UpstreamAccountCredentials{
        .account_id = account_id,
        .access_token = access_token,
    };
}

std::optional<UpstreamAccountCredentials> query_exact_account_credentials(
    sqlite3* db,
    const std::string_view account_id
) {
    if (db == nullptr || account_id.empty()) {
        return std::nullopt;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kFindExactCredentialSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return std::nullopt;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };

    if (sqlite3_bind_text(stmt, 1, std::string(account_id).c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        finalize();
        return std::nullopt;
    }

    auto credentials = read_credentials_row(stmt);
    finalize();
    return credentials;
}

std::optional<UpstreamAccountCredentials> query_latest_active_account_credentials(sqlite3* db) {
    if (db == nullptr) {
        return std::nullopt;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kFindAnyCredentialSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return std::nullopt;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };

    auto credentials = read_credentials_row(stmt);
    finalize();
    return credentials;
}

} // namespace

StickyAffinityResolution
resolve_sticky_affinity(const std::string& raw_request_body, const openai::HeaderMap& inbound_headers) {
    StickyAffinityResolution resolution;
    resolution.sticky_key = extract_sticky_key(raw_request_body);
    resolution.account_id = account_from_headers(inbound_headers);
    resolution.from_header = !resolution.account_id.empty();
    if (!resolution.account_id.empty() || resolution.sticky_key.empty()) {
        return resolution;
    }

    auto& state = sticky_db_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    sqlite3* db = ensure_db(state);
    if (db == nullptr) {
        return resolution;
    }

    const auto now = now_ms();
    maybe_purge_expired(state, db, now);
    auto persisted = db::find_proxy_sticky_session_account(db, resolution.sticky_key, now);
    if (persisted.has_value()) {
        resolution.account_id = *persisted;
        resolution.from_persistence = true;
    }
    return resolution;
}

void persist_sticky_affinity(const StickyAffinityResolution& resolution) {
    if (resolution.sticky_key.empty() || resolution.account_id.empty()) {
        return;
    }

    auto& state = sticky_db_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    sqlite3* db = ensure_db(state);
    if (db == nullptr) {
        return;
    }

    const auto now = now_ms();
    (void)db::upsert_proxy_sticky_session(db, resolution.sticky_key, resolution.account_id, now, kStickyTtlMs);
    maybe_purge_expired(state, db, now);
}

std::optional<UpstreamAccountCredentials> resolve_upstream_account_credentials(const std::string_view preferred_account_id) {
    auto& state = sticky_db_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    sqlite3* db = ensure_db(state);
    if (db == nullptr) {
        return std::nullopt;
    }
    if (!db::ensure_accounts_schema(db)) {
        return std::nullopt;
    }

    if (!preferred_account_id.empty()) {
        if (auto credentials = query_exact_account_credentials(db, preferred_account_id); credentials.has_value()) {
            return credentials;
        }
    }

    return query_latest_active_account_credentials(db);
}

std::optional<UpstreamAccountCredentials> refresh_upstream_account_credentials(const std::string_view account_id) {
    if (account_id.empty()) {
        return std::nullopt;
    }

    auto& state = sticky_db_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    sqlite3* db = ensure_db(state);
    if (db == nullptr) {
        return std::nullopt;
    }
    if (!db::ensure_accounts_schema(db)) {
        return std::nullopt;
    }

    const auto refreshed = auth::oauth::refresh_access_token_for_account(db, account_id);
    if (!refreshed.refreshed) {
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "oauth_token_refresh_failed",
            "account_id=" + std::string(account_id) + " code=" + refreshed.error_code
        );
        return std::nullopt;
    }
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy",
        "oauth_token_refreshed",
        "account_id=" + std::string(account_id)
    );
    return query_exact_account_credentials(db, account_id);
}

} // namespace tightrope::proxy::session
