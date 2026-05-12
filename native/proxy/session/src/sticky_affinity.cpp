#include "sticky_affinity.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glaze/glaze.hpp>

#include "config_loader.h"
#include "text/ascii.h"
#include "connection/sqlite_pool.h"
#include "cost_calculator.h"
#include "logging/logger.h"
#include "oauth/token_refresh.h"
#include "repositories/account_repo.h"
#include "repositories/request_log_repo.h"
#include "repositories/settings_repo.h"
#include "repositories/session_repo.h"
#include "sync_event_emitter.h"
#include "token_store.h"
#include "strategies/round_robin.h"
#include "strategies/weighted_round_robin.h"
#include "time/clock.h"

namespace tightrope::proxy::session {

namespace {

using Json = glz::generic;
using JsonObject = Json::object_t;

constexpr std::int64_t kDefaultStickyTtlMs = 30 * 60 * 1000;
constexpr std::int64_t kDefaultCleanupIntervalMs = 60 * 1000;
constexpr std::string_view kAccountHeader = "chatgpt-account-id";
constexpr std::string_view kStickyKindStickyThread = "sticky_thread";
constexpr std::string_view kStickyKindCodexSession = "codex_session";
constexpr std::string_view kStickyKindPromptCache = "prompt_cache";

constexpr std::array<std::string_view, 6> kStickyKeyFields = {
    "prompt_cache_key",
    "promptCacheKey",
    "session_id",
    "sessionId",
    "thread_id",
    "threadId",
};
constexpr std::array<std::string_view, 7> kStickyKeyHeaderFields = {
    "x-codex-session-id",
    "x-codex-turn-state",
    "session_id",
    "session-id",
    "x-session-id",
    "thread_id",
    "x-thread-id",
};
constexpr std::array<std::string_view, 3> kModelFields = {"model", "model_slug", "modelSlug"};
constexpr std::string_view kPlanModelPricingEnv = "TIGHTROPE_ROUTING_PLAN_MODEL_PRICING_USD_PER_MILLION";

struct RequestAffinityFields {
    std::string sticky_key;
    std::string sticky_kind = std::string(kStickyKindStickyThread);
    std::string model;
};

struct StickyKeyExtraction {
    std::string key;
    std::string kind = std::string(kStickyKindStickyThread);
};

struct PlanModelPricingOverrideState {
    std::mutex mutex;
    std::string raw_env_value;
    std::string raw_settings_value;
    std::unordered_map<std::string, usage::TokenPricingUsdPerMillion> pricing_by_key;
};

constexpr const char* kFindExactCredentialSql = R"SQL(
SELECT id, chatgpt_account_id, access_token_encrypted
FROM accounts
WHERE status = 'active'
  AND (quota_primary_percent IS NULL OR quota_primary_percent < 100)
  AND (quota_secondary_percent IS NULL OR quota_secondary_percent < 100)
  AND chatgpt_account_id = ?1
  AND chatgpt_account_id IS NOT NULL
  AND trim(chatgpt_account_id) != ''
  AND access_token_encrypted IS NOT NULL
  AND length(access_token_encrypted) > 0
LIMIT 1;
)SQL";

constexpr const char* kFindCredentialByInternalIdSql = R"SQL(
SELECT id, chatgpt_account_id, access_token_encrypted
FROM accounts
WHERE status = 'active'
  AND (quota_primary_percent IS NULL OR quota_primary_percent < 100)
  AND (quota_secondary_percent IS NULL OR quota_secondary_percent < 100)
  AND id = ?1
  AND chatgpt_account_id IS NOT NULL
  AND trim(chatgpt_account_id) != ''
  AND access_token_encrypted IS NOT NULL
  AND length(access_token_encrypted) > 0
LIMIT 1;
)SQL";

constexpr const char* kFindAccountRecordByChatgptIdSql = R"SQL(
SELECT 1
FROM accounts
WHERE chatgpt_account_id = ?1
  AND chatgpt_account_id IS NOT NULL
  AND trim(chatgpt_account_id) != ''
LIMIT 1;
)SQL";

constexpr const char* kFindAccountRecordByInternalIdSql = R"SQL(
SELECT 1
FROM accounts
WHERE id = ?1
LIMIT 1;
)SQL";

constexpr const char* kFindAnyCredentialSql = R"SQL(
SELECT id, chatgpt_account_id, access_token_encrypted
FROM accounts
WHERE status = 'active'
  AND (quota_primary_percent IS NULL OR quota_primary_percent < 100)
  AND (quota_secondary_percent IS NULL OR quota_secondary_percent < 100)
  AND chatgpt_account_id IS NOT NULL
  AND trim(chatgpt_account_id) != ''
  AND access_token_encrypted IS NOT NULL
  AND length(access_token_encrypted) > 0
ORDER BY updated_at DESC, id DESC
LIMIT 1;
)SQL";

constexpr const char* kFindPinnedCredentialSql = R"SQL(
SELECT id, chatgpt_account_id, access_token_encrypted
FROM accounts
WHERE routing_pinned = 1
  AND status = 'active'
  AND (quota_primary_percent IS NULL OR quota_primary_percent < 100)
  AND (quota_secondary_percent IS NULL OR quota_secondary_percent < 100)
  AND chatgpt_account_id IS NOT NULL
  AND trim(chatgpt_account_id) != ''
  AND access_token_encrypted IS NOT NULL
  AND length(access_token_encrypted) > 0
ORDER BY updated_at DESC, id DESC
LIMIT 1;
)SQL";

constexpr const char* kFindRoutedCredentialSql = R"SQL(
SELECT id,
       chatgpt_account_id,
       access_token_encrypted,
       quota_primary_percent,
       quota_secondary_percent,
       plan_type,
       quota_primary_reset_at_ms,
       quota_secondary_reset_at_ms
FROM accounts
WHERE status = 'active'
  AND (quota_primary_percent IS NULL OR quota_primary_percent < 100)
  AND (quota_secondary_percent IS NULL OR quota_secondary_percent < 100)
  AND chatgpt_account_id IS NOT NULL
  AND trim(chatgpt_account_id) != ''
  AND access_token_encrypted IS NOT NULL
  AND length(access_token_encrypted) > 0
ORDER BY updated_at DESC, id DESC;
)SQL";

constexpr const char* kFindRoutedCredentialWithRecentSuccessSql = R"SQL(
SELECT id,
       chatgpt_account_id,
       access_token_encrypted,
       quota_primary_percent,
       quota_secondary_percent,
       plan_type,
       quota_primary_reset_at_ms,
       quota_secondary_reset_at_ms,
       COALESCE(
           (
               SELECT AVG(
                          CASE
                              WHEN recent.status_code >= 200 AND recent.status_code < 500 THEN 1.0
                              ELSE 0.0
                          END
                      )
               FROM (
                   SELECT status_code
                   FROM request_logs
                   WHERE account_id = accounts.id
                   ORDER BY id DESC
                   LIMIT 64
               ) AS recent
           ),
           1.0
       ) AS recent_success_rate
FROM accounts
WHERE status = 'active'
  AND (quota_primary_percent IS NULL OR quota_primary_percent < 100)
  AND (quota_secondary_percent IS NULL OR quota_secondary_percent < 100)
  AND chatgpt_account_id IS NOT NULL
  AND trim(chatgpt_account_id) != ''
  AND access_token_encrypted IS NOT NULL
  AND length(access_token_encrypted) > 0
ORDER BY updated_at DESC, id DESC;
)SQL";

constexpr const char* kMarkAccountUnusableByChatgptIdSql = R"SQL(
UPDATE accounts
SET status = 'deactivated',
    routing_pinned = 0,
    updated_at = datetime('now')
WHERE chatgpt_account_id = ?1
  AND (status != 'deactivated' OR routing_pinned != 0);
)SQL";

constexpr const char* kMarkAccountExhaustedByChatgptIdSql = R"SQL(
UPDATE accounts
SET status = CASE
        WHEN status IN ('paused', 'deactivated') THEN status
        ELSE ?2
    END,
    quota_primary_percent = CASE
        WHEN ?2 IN ('rate_limited', 'quota_blocked') THEN 100
        ELSE quota_primary_percent
    END,
    quota_secondary_percent = CASE
        WHEN ?2 = 'quota_blocked' THEN 100
        ELSE quota_secondary_percent
    END,
    routing_pinned = CASE
        WHEN status IN ('paused', 'deactivated') THEN routing_pinned
        ELSE 0
    END,
    usage_telemetry_refreshed_at = datetime('now'),
    updated_at = datetime('now')
WHERE chatgpt_account_id = ?1;
)SQL";

constexpr const char* kUpdateAccountAccessTokenByIdSql = R"SQL(
UPDATE accounts
SET access_token_encrypted = ?1,
    updated_at = datetime('now')
WHERE id = ?2;
)SQL";

struct RoutedCredentialCandidate {
    UpstreamAccountCredentials credentials;
    std::optional<int> quota_primary_percent;
    std::optional<int> quota_secondary_percent;
    std::optional<std::string> plan_type;
    std::optional<std::int64_t> quota_primary_reset_at_ms;
    std::optional<std::int64_t> quota_secondary_reset_at_ms;
    std::optional<double> recent_success_rate;
};

struct StickyDbState {
    std::mutex mutex;
    std::string db_path;
    std::unique_ptr<db::SqlitePool> pool;
    bool schema_ready = false;
    bool settings_schema_ready = false;
    bool sticky_threads_enabled = false;
    bool strict_lock_pool_continuations = false;
    std::string upstream_stream_transport = "default";
    std::int64_t last_cleanup_ms = 0;
    std::int64_t sticky_ttl_ms = kDefaultStickyTtlMs;
    std::int64_t cleanup_interval_ms = kDefaultCleanupIntervalMs;
    std::size_t round_robin_cursor = 0;
    std::size_t drain_hop_cursor = 0;
    std::string drain_hop_account_id;
    balancer::WeightedRoundRobinPicker weighted_round_robin_picker{};
};

core::time::Clock& runtime_clock() {
    static core::time::SystemClock clock;
    return clock;
}

std::int64_t now_ms() {
    return runtime_clock().unix_ms_now();
}

void emit_runtime_signal(
    const std::string_view level,
    const std::string_view code,
    const std::string_view message,
    const std::string_view account_id = {}
) {
    sync::SyncEventEmitter::get().emit(sync::SyncEventRuntimeSignal{
        .level = std::string(level),
        .code = std::string(code),
        .message = std::string(message),
        .account_id = std::string(account_id),
    });
}

StickyDbState& sticky_db_state() {
    static StickyDbState state;
    return state;
}

PlanModelPricingOverrideState& plan_model_pricing_override_state() {
    static PlanModelPricingOverrideState state;
    return state;
}

std::string read_env(const char* key) {
    if (key == nullptr) {
        return {};
    }
    const auto* value = std::getenv(key);
    if (value == nullptr || value[0] == '\0') {
        return {};
    }
    return std::string(value);
}

bool parse_non_negative_double(std::string_view raw, double* out) {
    if (out == nullptr) {
        return false;
    }
    auto value = std::string(core::text::trim_ascii(raw));
    if (value.empty()) {
        return false;
    }

    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || end == nullptr || *end != '\0') {
        return false;
    }
    if (!std::isfinite(parsed) || parsed < 0.0 || parsed > std::numeric_limits<double>::max()) {
        return false;
    }
    *out = parsed;
    return true;
}

std::unordered_map<std::string, usage::TokenPricingUsdPerMillion> parse_plan_model_pricing_overrides(
    const std::string& raw_value
) {
    std::unordered_map<std::string, usage::TokenPricingUsdPerMillion> overrides;
    std::size_t cursor = 0;
    while (cursor < raw_value.size()) {
        const auto delimiter = raw_value.find_first_of(",;", cursor);
        const auto token_view = delimiter == std::string::npos
                                    ? std::string_view(raw_value).substr(cursor)
                                    : std::string_view(raw_value).substr(cursor, delimiter - cursor);
        const auto token = std::string(core::text::trim_ascii(token_view));
        if (!token.empty()) {
            const auto equals = token.find('=');
            if (equals != std::string::npos) {
                auto lhs = core::text::to_lower_ascii(core::text::trim_ascii(token.substr(0, equals)));
                auto rhs = std::string(core::text::trim_ascii(token.substr(equals + 1)));
                const auto at = lhs.find('@');
                if (at != std::string::npos && !rhs.empty()) {
                    auto plan = core::text::trim_ascii(lhs.substr(0, at));
                    auto model = core::text::trim_ascii(lhs.substr(at + 1));
                    const auto separator = rhs.find(':');
                    if (!plan.empty() && !model.empty() && separator != std::string::npos) {
                        double input = 0.0;
                        double output = 0.0;
                        const auto input_raw = rhs.substr(0, separator);
                        const auto output_raw = rhs.substr(separator + 1);
                        if (parse_non_negative_double(input_raw, &input) &&
                            parse_non_negative_double(output_raw, &output)) {
                            overrides[std::string(plan) + "@" + std::string(model)] = {
                                .input = input,
                                .output = output,
                            };
                        }
                    }
                }
            }
        }

        if (delimiter == std::string::npos) {
            break;
        }
        cursor = delimiter + 1;
    }
    return overrides;
}

std::optional<usage::TokenPricingUsdPerMillion> lookup_plan_model_pricing_override(
    std::string_view plan,
    std::string_view model,
    std::string_view settings_overrides
) {
    auto normalized_plan = core::text::to_lower_ascii(core::text::trim_ascii(plan));
    auto normalized_model = core::text::to_lower_ascii(core::text::trim_ascii(model));
    if (normalized_plan.empty()) {
        return std::nullopt;
    }

    auto& state = plan_model_pricing_override_state();
    const auto raw_env_value = read_env(kPlanModelPricingEnv.data());
    const auto raw_settings_value = std::string(settings_overrides);
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.raw_env_value != raw_env_value || state.raw_settings_value != raw_settings_value) {
        state.raw_env_value = raw_env_value;
        state.raw_settings_value = raw_settings_value;
        auto merged = parse_plan_model_pricing_overrides(raw_settings_value);
        const auto env_overrides = parse_plan_model_pricing_overrides(raw_env_value);
        for (const auto& [key, pricing] : env_overrides) {
            merged[key] = pricing;
        }
        state.pricing_by_key = std::move(merged);
    }

    if (!normalized_model.empty()) {
        const auto exact_key = normalized_plan + "@" + normalized_model;
        if (const auto exact = state.pricing_by_key.find(exact_key); exact != state.pricing_by_key.end()) {
            return exact->second;
        }
    }

    const auto wildcard_key = normalized_plan + "@*";
    if (const auto wildcard = state.pricing_by_key.find(wildcard_key); wildcard != state.pricing_by_key.end()) {
        return wildcard->second;
    }

    return std::nullopt;
}

std::string normalize_upstream_stream_transport(std::string_view transport);

void recover_expired_account_limit_blocks(sqlite3* db, const std::int64_t captured_now_ms) {
    if (db == nullptr || captured_now_ms <= 0) {
        return;
    }

    constexpr const char* kSql = R"SQL(
UPDATE accounts
SET status = 'active',
    quota_primary_percent = CASE
        WHEN quota_primary_reset_at_ms IS NOT NULL AND quota_primary_reset_at_ms <= ?1 THEN NULL
        WHEN status = 'quota_blocked'
             AND quota_secondary_reset_at_ms IS NOT NULL
             AND quota_secondary_reset_at_ms <= ?1 THEN NULL
        ELSE quota_primary_percent
    END,
    quota_secondary_percent = CASE
        WHEN quota_secondary_reset_at_ms IS NOT NULL AND quota_secondary_reset_at_ms <= ?1 THEN NULL
        WHEN status = 'quota_blocked'
             AND quota_secondary_reset_at_ms IS NULL
             AND quota_primary_reset_at_ms IS NOT NULL
             AND quota_primary_reset_at_ms <= ?1 THEN NULL
        ELSE quota_secondary_percent
    END,
    updated_at = datetime('now')
WHERE status IN ('rate_limited', 'quota_blocked')
  AND (
      (status = 'rate_limited'
       AND quota_primary_reset_at_ms IS NOT NULL
       AND quota_primary_reset_at_ms <= ?1)
      OR
      (status = 'quota_blocked'
       AND (
           (quota_secondary_reset_at_ms IS NOT NULL AND quota_secondary_reset_at_ms <= ?1)
           OR
           (quota_secondary_reset_at_ms IS NULL
            AND quota_primary_reset_at_ms IS NOT NULL
            AND quota_primary_reset_at_ms <= ?1)
       ))
  );
)SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };
    if (sqlite3_bind_int64(stmt, 1, captured_now_ms) != SQLITE_OK) {
        finalize();
        return;
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        finalize();
        return;
    }
    const auto rows_updated = sqlite3_changes(db);
    finalize();
    if (rows_updated > 0) {
        core::logging::log_event(
            core::logging::LogLevel::Info,
            "runtime",
            "proxy",
            "account_limit_blocks_recovered",
            "count=" + std::to_string(rows_updated)
        );
        emit_runtime_signal(
            "info",
            "account_limit_blocks_recovered",
            "recovered expired account limit blocks count=" + std::to_string(rows_updated)
        );
    }
}

sqlite3* ensure_db(StickyDbState& state) {
    const auto config = config::load_config();
    const auto desired_path = config.db_path.empty() ? std::string("store.db") : config.db_path;
    state.sticky_ttl_ms = std::max<std::int64_t>(1, config.sticky_ttl_ms);
    state.cleanup_interval_ms = std::max<std::int64_t>(1, config.sticky_cleanup_interval_ms);

    if (!state.pool || state.db_path != desired_path) {
        if (state.pool) {
            state.pool->close();
        }
        state.pool = std::make_unique<db::SqlitePool>(desired_path);
        state.db_path = desired_path;
        state.schema_ready = false;
        state.settings_schema_ready = false;
        state.last_cleanup_ms = 0;
        state.round_robin_cursor = 0;
        state.drain_hop_cursor = 0;
        state.drain_hop_account_id.clear();
        state.upstream_stream_transport = "default";
        state.weighted_round_robin_picker = balancer::WeightedRoundRobinPicker{};
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

    if (!state.settings_schema_ready) {
        state.settings_schema_ready = db::ensure_dashboard_settings_schema(db);
    }
    if (state.settings_schema_ready) {
        if (const auto settings = db::get_dashboard_settings(db); settings.has_value()) {
            state.sticky_threads_enabled = settings->sticky_threads_enabled;
            state.strict_lock_pool_continuations = settings->strict_lock_pool_continuations;
            state.upstream_stream_transport = normalize_upstream_stream_transport(settings->upstream_stream_transport);
            if (settings->openai_cache_affinity_max_age_seconds > 0) {
                constexpr std::int64_t kMsPerSecond = 1000;
                const auto max_seconds = std::numeric_limits<std::int64_t>::max() / kMsPerSecond;
                const auto ttl_seconds = std::min<std::int64_t>(settings->openai_cache_affinity_max_age_seconds, max_seconds);
                state.sticky_ttl_ms = std::max<std::int64_t>(1, ttl_seconds * kMsPerSecond);
            }
        } else {
            state.sticky_threads_enabled = false;
            state.strict_lock_pool_continuations = false;
            state.upstream_stream_transport = "default";
        }
    } else {
        state.sticky_threads_enabled = false;
        state.strict_lock_pool_continuations = false;
        state.upstream_stream_transport = "default";
    }

    return db;
}

void maybe_purge_expired(StickyDbState& state, sqlite3* db, const std::int64_t now) {
    if (now < state.last_cleanup_ms) {
        state.last_cleanup_ms = now;
        return;
    }
    if (now - state.last_cleanup_ms < state.cleanup_interval_ms) {
        return;
    }
    (void)db::purge_expired_proxy_sticky_sessions(db, now);
    state.last_cleanup_ms = now;
}

std::unique_lock<std::mutex> acquire_sticky_state_lock(std::mutex& mutex, std::string_view operation) {
    static_cast<void>(operation);
    std::unique_lock<std::mutex> lock(mutex);
    return lock;
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

std::string normalize_sticky_kind(std::string_view kind) {
    auto normalized = core::text::to_lower_ascii(core::text::trim_ascii(kind));
    if (normalized == kStickyKindPromptCache || normalized == kStickyKindCodexSession ||
        normalized == kStickyKindStickyThread) {
        return normalized;
    }
    return std::string(kStickyKindStickyThread);
}

std::string_view sticky_kind_for_json_key(std::string_view key) {
    if (key == "prompt_cache_key" || key == "promptCacheKey") {
        return kStickyKindPromptCache;
    }
    if (key == "session_id" || key == "sessionId" || key == "thread_id" || key == "threadId") {
        return kStickyKindCodexSession;
    }
    return kStickyKindStickyThread;
}

std::string_view sticky_kind_for_header_key(std::string_view key) {
    if (key == "x-codex-session-id" || key == "x-codex-turn-state" || key == "session_id" || key == "session-id" ||
        key == "x-session-id" || key == "thread_id" || key == "x-thread-id") {
        return kStickyKindCodexSession;
    }
    return kStickyKindStickyThread;
}

StickyKeyExtraction sticky_key_from_object(const JsonObject& object) {
    for (const auto key : kStickyKeyFields) {
        auto value = read_string_field(object, key);
        if (!value.empty()) {
            return StickyKeyExtraction{
                .key = std::move(value),
                .kind = std::string(sticky_kind_for_json_key(key)),
            };
        }
    }

    const auto metadata_it = object.find("metadata");
    if (metadata_it != object.end() && metadata_it->second.is_object()) {
        const auto& metadata = metadata_it->second.get_object();
        for (const auto key : kStickyKeyFields) {
            auto value = read_string_field(metadata, key);
            if (!value.empty()) {
                return StickyKeyExtraction{
                    .key = std::move(value),
                    .kind = std::string(sticky_kind_for_json_key(key)),
                };
            }
        }
    }

    return StickyKeyExtraction{};
}

std::string model_from_object(const JsonObject& object) {
    for (const auto key : kModelFields) {
        auto value = core::text::trim_ascii(read_string_field(object, key));
        if (!value.empty()) {
            return std::string(value);
        }
    }
    return {};
}

RequestAffinityFields extract_request_affinity_fields(const std::string& raw_request_body) {
    RequestAffinityFields fields;
    Json payload{};
    if (const auto ec = glz::read_json(payload, raw_request_body); ec) {
        return fields;
    }
    if (!payload.is_object()) {
        return fields;
    }
    const auto& object = payload.get_object();
    const auto sticky = sticky_key_from_object(object);
    fields.sticky_key = sticky.key;
    fields.sticky_kind = normalize_sticky_kind(sticky.kind);
    fields.model = model_from_object(object);
    return fields;
}

std::string account_from_headers(const openai::HeaderMap& inbound_headers) {
    for (const auto& [name, value] : inbound_headers) {
        if (core::text::equals_case_insensitive(name, kAccountHeader) && !value.empty()) {
            return value;
        }
    }
    return {};
}

StickyKeyExtraction sticky_key_from_headers(const openai::HeaderMap& inbound_headers) {
    for (const auto key : kStickyKeyHeaderFields) {
        for (const auto& [name, value] : inbound_headers) {
            if (!core::text::equals_case_insensitive(name, key)) {
                continue;
            }
            auto candidate = std::string(core::text::trim_ascii(value));
            if (!candidate.empty()) {
                return StickyKeyExtraction{
                    .key = std::move(candidate),
                    .kind = std::string(sticky_kind_for_header_key(key)),
                };
            }
        }
    }
    return StickyKeyExtraction{};
}

std::optional<std::string> normalize_stored_access_token(
    sqlite3* db,
    std::int64_t internal_account_id,
    const std::string& stored_access_token
);

std::optional<UpstreamAccountCredentials> read_credentials_row(sqlite3* db, sqlite3_stmt* stmt) {
    if (stmt == nullptr || sqlite3_step(stmt) != SQLITE_ROW) {
        return std::nullopt;
    }
    const auto internal_account_id = sqlite3_column_int64(stmt, 0);
    const auto* account_raw = sqlite3_column_text(stmt, 1);
    const auto* token_raw = sqlite3_column_text(stmt, 2);
    if (account_raw == nullptr || token_raw == nullptr) {
        return std::nullopt;
    }
    const auto account_id = std::string(core::text::trim_ascii(reinterpret_cast<const char*>(account_raw)));
    const auto stored_access_token_raw = std::string(core::text::trim_ascii(reinterpret_cast<const char*>(token_raw)));
    const auto stored_access_token = normalize_stored_access_token(db, internal_account_id, stored_access_token_raw);
    if (!stored_access_token.has_value()) {
        return std::nullopt;
    }
    std::string token_error;
    const auto decrypted_access_token =
        ::tightrope::auth::crypto::decrypt_token_from_storage(*stored_access_token, &token_error);
    if (!decrypted_access_token.has_value()) {
        return std::nullopt;
    }
    const auto access_token = std::string(core::text::trim_ascii(*decrypted_access_token));
    if (account_id.empty() || access_token.empty()) {
        return std::nullopt;
    }
    return UpstreamAccountCredentials{
        .account_id = account_id,
        .access_token = access_token,
        .internal_account_id = internal_account_id,
    };
}

std::vector<std::string> parse_locked_routing_account_ids(std::string_view raw) {
    std::vector<std::string> account_ids;
    std::unordered_set<std::string> seen;
    auto remaining = std::string(core::text::trim_ascii(raw));
    while (!remaining.empty()) {
        const auto delimiter = remaining.find(',');
        const auto token = delimiter == std::string::npos ? remaining : remaining.substr(0, delimiter);
        auto normalized = std::string(core::text::trim_ascii(token));
        if (!normalized.empty() && seen.insert(normalized).second) {
            account_ids.push_back(std::move(normalized));
        }
        if (delimiter == std::string::npos) {
            break;
        }
        remaining = remaining.substr(delimiter + 1);
    }
    return account_ids;
}

std::optional<std::int64_t> parse_positive_int64(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }
    std::int64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed <= 0) {
        return std::nullopt;
    }
    return parsed;
}

bool account_record_exists_by_chatgpt_id(sqlite3* db, const std::string_view account_id) {
    if (db == nullptr || account_id.empty()) {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kFindAccountRecordByChatgptIdSql, -1, &stmt, nullptr) != SQLITE_OK ||
        stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return false;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };

    const auto normalized = std::string(account_id);
    if (sqlite3_bind_text(stmt, 1, normalized.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        finalize();
        return false;
    }

    const bool exists = sqlite3_step(stmt) == SQLITE_ROW;
    finalize();
    return exists;
}

bool account_record_exists_by_internal_id(sqlite3* db, const std::int64_t account_id) {
    if (db == nullptr || account_id <= 0) {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kFindAccountRecordByInternalIdSql, -1, &stmt, nullptr) != SQLITE_OK ||
        stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return false;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };

    if (sqlite3_bind_int64(stmt, 1, account_id) != SQLITE_OK) {
        finalize();
        return false;
    }

    const bool exists = sqlite3_step(stmt) == SQLITE_ROW;
    finalize();
    return exists;
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

    auto credentials = read_credentials_row(db, stmt);
    finalize();
    return credentials;
}

std::optional<UpstreamAccountCredentials> query_internal_account_credentials(sqlite3* db, const std::int64_t account_id) {
    if (db == nullptr || account_id <= 0) {
        return std::nullopt;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kFindCredentialByInternalIdSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
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

    if (sqlite3_bind_int64(stmt, 1, account_id) != SQLITE_OK) {
        finalize();
        return std::nullopt;
    }

    auto credentials = read_credentials_row(db, stmt);
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

    auto credentials = read_credentials_row(db, stmt);
    finalize();
    return credentials;
}

std::optional<UpstreamAccountCredentials> query_pinned_account_credentials(sqlite3* db) {
    if (db == nullptr) {
        return std::nullopt;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kFindPinnedCredentialSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
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

    auto credentials = read_credentials_row(db, stmt);
    finalize();
    return credentials;
}

std::optional<int> optional_int_column(sqlite3_stmt* stmt, const int index) {
    if (stmt == nullptr || sqlite3_column_type(stmt, index) == SQLITE_NULL) {
        return std::nullopt;
    }
    return sqlite3_column_int(stmt, index);
}

std::optional<std::int64_t> optional_int64_column(sqlite3_stmt* stmt, const int index) {
    if (stmt == nullptr || sqlite3_column_type(stmt, index) == SQLITE_NULL) {
        return std::nullopt;
    }
    return sqlite3_column_int64(stmt, index);
}

std::optional<double> optional_double_column(sqlite3_stmt* stmt, const int index) {
    if (stmt == nullptr || sqlite3_column_type(stmt, index) == SQLITE_NULL) {
        return std::nullopt;
    }
    return sqlite3_column_double(stmt, index);
}

std::optional<std::string> optional_text_column(sqlite3_stmt* stmt, const int index) {
    if (stmt == nullptr) {
        return std::nullopt;
    }
    const auto* raw = sqlite3_column_text(stmt, index);
    if (raw == nullptr) {
        return std::nullopt;
    }
    auto value = std::string(core::text::trim_ascii(reinterpret_cast<const char*>(raw)));
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

std::vector<RoutedCredentialCandidate> query_routed_account_candidates(sqlite3* db) {
    std::vector<RoutedCredentialCandidate> candidates;
    if (db == nullptr) {
        return candidates;
    }

    const bool request_log_schema_ready = db::ensure_request_log_schema(db);
    const auto* routed_sql =
        request_log_schema_ready ? kFindRoutedCredentialWithRecentSuccessSql : kFindRoutedCredentialSql;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, routed_sql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
        if (routed_sql != kFindRoutedCredentialSql &&
            (sqlite3_prepare_v2(db, kFindRoutedCredentialSql, -1, &stmt, nullptr) == SQLITE_OK && stmt != nullptr)) {
            // Fallback: route even if the request-log success-rate query fails on unexpected schemas.
        } else {
            if (stmt != nullptr) {
                sqlite3_finalize(stmt);
            }
            return candidates;
        }
    }
    if (stmt == nullptr) {
        return candidates;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto internal_account_id = sqlite3_column_int64(stmt, 0);
        const auto* account_raw = sqlite3_column_text(stmt, 1);
        const auto* token_raw = sqlite3_column_text(stmt, 2);
        if (account_raw == nullptr || token_raw == nullptr) {
            continue;
        }

        auto account_id = std::string(core::text::trim_ascii(reinterpret_cast<const char*>(account_raw)));
        const auto stored_access_token_raw = std::string(core::text::trim_ascii(reinterpret_cast<const char*>(token_raw)));
        const auto stored_access_token = normalize_stored_access_token(db, internal_account_id, stored_access_token_raw);
        if (!stored_access_token.has_value()) {
            continue;
        }
        std::string token_error;
        auto decrypted_access_token =
            ::tightrope::auth::crypto::decrypt_token_from_storage(*stored_access_token, &token_error);
        if (!decrypted_access_token.has_value()) {
            continue;
        }
        auto access_token = std::string(core::text::trim_ascii(*decrypted_access_token));
        if (account_id.empty() || access_token.empty()) {
            continue;
        }

        RoutedCredentialCandidate candidate;
        candidate.credentials = UpstreamAccountCredentials{
            .account_id = std::move(account_id),
            .access_token = std::move(access_token),
            .internal_account_id = internal_account_id,
        };
        candidate.quota_primary_percent = optional_int_column(stmt, 3);
        candidate.quota_secondary_percent = optional_int_column(stmt, 4);
        candidate.plan_type = optional_text_column(stmt, 5);
        candidate.quota_primary_reset_at_ms = optional_int64_column(stmt, 6);
        candidate.quota_secondary_reset_at_ms = optional_int64_column(stmt, 7);
        if (sqlite3_column_count(stmt) > 8) {
            candidate.recent_success_rate = optional_double_column(stmt, 8);
        }
        candidates.push_back(std::move(candidate));
    }

    finalize();
    return candidates;
}

bool mark_account_unusable_by_chatgpt_id(sqlite3* db, const std::string_view account_id) {
    if (db == nullptr || account_id.empty()) {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kMarkAccountUnusableByChatgptIdSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return false;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };

    const auto normalized_id = std::string(core::text::trim_ascii(account_id));
    if (sqlite3_bind_text(stmt, 1, normalized_id.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        finalize();
        return false;
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        finalize();
        return false;
    }
    const auto rows_updated = sqlite3_changes(db);
    finalize();
    return rows_updated > 0;
}

std::string normalize_exhausted_status(const std::string_view status) {
    const auto normalized = core::text::to_lower_ascii(core::text::trim_ascii(status));
    if (normalized == "quota_blocked") {
        return "quota_blocked";
    }
    return "rate_limited";
}

bool mark_account_exhausted_by_chatgpt_id(
    sqlite3* db,
    const std::string_view account_id,
    const std::string_view status
) {
    if (db == nullptr || account_id.empty()) {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kMarkAccountExhaustedByChatgptIdSql, -1, &stmt, nullptr) != SQLITE_OK ||
        stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return false;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };

    const auto normalized_id = std::string(core::text::trim_ascii(account_id));
    const auto normalized_status = normalize_exhausted_status(status);
    if (sqlite3_bind_text(stmt, 1, normalized_id.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 2, normalized_status.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        finalize();
        return false;
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        finalize();
        return false;
    }
    const auto rows_updated = sqlite3_changes(db);
    finalize();
    return rows_updated > 0;
}

bool persist_migrated_access_token(sqlite3* db, const std::int64_t internal_account_id, const std::string& stored_token) {
    if (db == nullptr || internal_account_id <= 0 || stored_token.empty()) {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kUpdateAccountAccessTokenByIdSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return false;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };

    if (sqlite3_bind_text(stmt, 1, stored_token.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(stmt, 2, internal_account_id) != SQLITE_OK) {
        finalize();
        return false;
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        finalize();
        return false;
    }
    finalize();
    return true;
}

std::optional<std::string> normalize_stored_access_token(
    sqlite3* db,
    const std::int64_t internal_account_id,
    const std::string& stored_access_token
) {
    bool migrated = false;
    std::string migration_error;
    auto migrated_value = ::tightrope::auth::crypto::migrate_plaintext_token_for_storage(
        stored_access_token,
        &migrated,
        &migration_error
    );
    if (!migrated_value.has_value()) {
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "account_token_migration_failed",
            "account_internal_id=" + std::to_string(internal_account_id) + " reason=" + migration_error
        );
        return std::nullopt;
    }
    if (migrated) {
        const bool persisted = persist_migrated_access_token(db, internal_account_id, *migrated_value);
        core::logging::log_event(
            persisted ? core::logging::LogLevel::Info : core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "account_token_migration",
            "account_internal_id=" + std::to_string(internal_account_id) + " persisted=" + (persisted ? "true" : "false")
        );
        if (!persisted) {
            return std::nullopt;
        }
    }
    return migrated_value;
}

double usage_ratio_from_percent(const std::optional<int> percent) {
    if (!percent.has_value()) {
        return 0.0;
    }
    const auto clamped = std::clamp(*percent, 0, 100);
    return static_cast<double>(clamped) / 100.0;
}

std::optional<std::int64_t> next_reset_at_ms(const RoutedCredentialCandidate& candidate) {
    std::optional<std::int64_t> next;
    const auto promote = [&next](const std::optional<std::int64_t>& value) {
        if (!value.has_value() || *value <= 0) {
            return;
        }
        if (!next.has_value() || *value < *next) {
            next = *value;
        }
    };
    promote(candidate.quota_primary_reset_at_ms);
    promote(candidate.quota_secondary_reset_at_ms);
    return next;
}

double weighted_usage_ratio(
    const std::optional<int> primary_percent,
    const std::optional<int> secondary_percent,
    const db::DashboardSettingsRecord& settings
) {
    const auto primary_usage = usage_ratio_from_percent(primary_percent);
    const auto secondary_usage = secondary_percent.has_value()
                                     ? usage_ratio_from_percent(secondary_percent)
                                     : primary_usage;
    const auto primary_weight = std::max(0.0, settings.routing_headroom_weight_primary);
    const auto secondary_weight = std::max(0.0, settings.routing_headroom_weight_secondary);
    const auto total_weight = primary_weight + secondary_weight;
    if (total_weight <= std::numeric_limits<double>::epsilon()) {
        return primary_usage;
    }
    const auto weighted =
        ((primary_weight * primary_usage) + (secondary_weight * secondary_usage)) / total_weight;
    return std::clamp(weighted, 0.0, 1.0);
}

usage::TokenPricingUsdPerMillion default_pricing_for_plan(const std::optional<std::string>& plan_type) {
    if (!plan_type.has_value()) {
        return {.input = 0.15, .output = 0.45};
    }
    const auto normalized = core::text::to_lower_ascii(core::text::trim_ascii(*plan_type));
    if (normalized.empty()) {
        return {.input = 0.15, .output = 0.45};
    }
    if (normalized == "free") {
        return {.input = 0.04, .output = 0.06};
    }
    if (normalized == "plus") {
        return {.input = 0.10, .output = 0.25};
    }
    if (normalized == "pro") {
        return {.input = 0.15, .output = 0.35};
    }
    if (normalized == "team") {
        return {.input = 0.20, .output = 0.45};
    }
    if (normalized == "business") {
        return {.input = 0.30, .output = 0.50};
    }
    if (normalized == "enterprise") {
        return {.input = 0.40, .output = 0.60};
    }
    return {.input = 0.20, .output = 0.40};
}

usage::TokenPricingUsdPerMillion model_pricing_multipliers(const std::string_view model) {
    const auto normalized = core::text::to_lower_ascii(core::text::trim_ascii(model));
    if (normalized.empty()) {
        return {.input = 1.0, .output = 1.0};
    }
    if (normalized.find("mini") != std::string::npos) {
        return {.input = 0.55, .output = 0.50};
    }
    if (core::text::starts_with(normalized, "o")) {
        return {.input = 1.15, .output = 1.60};
    }
    if (core::text::starts_with(normalized, "gpt-5")) {
        return {.input = 1.10, .output = 1.40};
    }
    if (core::text::starts_with(normalized, "gpt-4.1")) {
        return {.input = 0.95, .output = 1.10};
    }
    return {.input = 1.0, .output = 1.0};
}

usage::TokenPricingUsdPerMillion resolve_pricing_for_plan_model(
    const std::optional<std::string>& plan_type,
    const std::string_view request_model,
    std::string_view settings_overrides
) {
    const auto normalized_plan = plan_type.has_value()
                                     ? core::text::to_lower_ascii(core::text::trim_ascii(*plan_type))
                                     : std::string();
    if (const auto override_pricing =
            lookup_plan_model_pricing_override(normalized_plan, request_model, settings_overrides);
        override_pricing.has_value()) {
        return *override_pricing;
    }

    auto pricing = default_pricing_for_plan(plan_type);
    const auto multipliers = model_pricing_multipliers(request_model);
    pricing.input *= std::max(0.0, multipliers.input);
    pricing.output *= std::max(0.0, multipliers.output);
    return pricing;
}

double normalized_plan_cost(
    const std::optional<std::string>& plan_type,
    const std::string_view request_model,
    std::string_view settings_overrides
) {
    using tightrope::usage::RequestTokenUsage;

    static const RequestTokenUsage kReferenceUsage = {
        .input_tokens = 1'000'000,
        .output_tokens = 1'000'000,
    };
    const auto enterprise_pricing =
        resolve_pricing_for_plan_model(std::string("enterprise"), request_model, settings_overrides);
    const auto kEnterpriseReferenceCostUsd =
        usage::estimate_request_cost_usd(kReferenceUsage, enterprise_pricing).total_cost_usd;
    if (kEnterpriseReferenceCostUsd <= 0.0) {
        return 0.5;
    }

    const auto total_cost = usage::estimate_request_cost_usd(
                                kReferenceUsage,
                                resolve_pricing_for_plan_model(plan_type, request_model, settings_overrides)
    )
                                .total_cost_usd;
    return std::clamp(total_cost / kEnterpriseReferenceCostUsd, 0.0, 1.0);
}

std::string normalize_supported_routing_strategy(std::string_view strategy) {
    auto normalized = core::text::to_lower_ascii(core::text::trim_ascii(strategy));
    if (normalized == "round_robin" || normalized == "weighted_round_robin" || normalized == "drain_hop") {
        return normalized;
    }
    // Backward compatibility: legacy strategies now route through weighted round robin.
    return "weighted_round_robin";
}

std::string normalize_upstream_stream_transport(std::string_view transport) {
    auto normalized = core::text::to_lower_ascii(core::text::trim_ascii(transport));
    if (normalized == "default" || normalized == "auto" || normalized == "http" || normalized == "websocket") {
        return normalized;
    }
    return "default";
}

const balancer::AccountCandidate*
pick_drain_hop_candidate(StickyDbState& state, const std::vector<balancer::AccountCandidate>& candidates) {
    if (candidates.empty()) {
        return nullptr;
    }

    if (!state.drain_hop_account_id.empty()) {
        const auto existing = std::find_if(
            candidates.begin(),
            candidates.end(),
            [&state](const balancer::AccountCandidate& candidate) { return candidate.id == state.drain_hop_account_id; }
        );
        if (existing != candidates.end()) {
            return &(*existing);
        }
        state.drain_hop_account_id.clear();
    }

    const auto index = state.drain_hop_cursor % candidates.size();
    const auto* selected = &candidates[index];
    state.drain_hop_cursor = (index + 1) % candidates.size();
    state.drain_hop_account_id = selected->id;
    return selected;
}

std::vector<balancer::AccountCandidate> to_balancer_candidates(
    const std::vector<RoutedCredentialCandidate>& routed_candidates,
    const std::string_view request_model,
    const db::DashboardSettingsRecord& settings
) {
    std::vector<balancer::AccountCandidate> candidates;
    candidates.reserve(routed_candidates.size());
    const auto current_ms = now_ms();
    constexpr std::int64_t kPreferenceHorizonMs = 7LL * 24LL * 60LL * 60LL * 1000LL;
    constexpr double kMaxResetBias = 0.5;
    constexpr double kMinSuccessInfluence = 0.05;
    const auto success_rho = std::clamp(settings.routing_success_rate_rho, 0.01, 64.0);
    for (const auto& candidate : routed_candidates) {
        balancer::AccountCandidate balancer_candidate;
        balancer_candidate.id = candidate.credentials.account_id;
        balancer_candidate.active = true;
        balancer_candidate.healthy = true;
        balancer_candidate.enabled = true;
        balancer_candidate.usage_ratio = weighted_usage_ratio(
            candidate.quota_primary_percent,
            candidate.quota_secondary_percent,
            settings
        );
        if (candidate.quota_secondary_percent.has_value()) {
            balancer_candidate.secondary_usage_ratio = usage_ratio_from_percent(candidate.quota_secondary_percent);
        }
        balancer_candidate.normalized_cost = normalized_plan_cost(
            candidate.plan_type,
            request_model,
            settings.routing_plan_model_pricing_usd_per_million
        );
        if (settings.prefer_earlier_reset_accounts) {
            if (const auto reset_at = next_reset_at_ms(candidate); reset_at.has_value()) {
                double urgency = 1.0;
                if (*reset_at > current_ms) {
                    const auto remaining_ms = *reset_at - current_ms;
                    const auto clamped_remaining = std::clamp(remaining_ms, static_cast<std::int64_t>(0), kPreferenceHorizonMs);
                    const auto fraction = static_cast<double>(clamped_remaining) / static_cast<double>(kPreferenceHorizonMs);
                    urgency = 1.0 - fraction;
                }
                balancer_candidate.static_weight = std::clamp(1.0 + (urgency * kMaxResetBias), 0.0, 4.0);
            }
        }
        const auto success_rate = std::clamp(candidate.recent_success_rate.value_or(1.0), 0.0, 1.0);
        balancer_candidate.success_rate = success_rate;
        const auto success_bias = std::max(kMinSuccessInfluence, std::pow(success_rate, success_rho));
        balancer_candidate.static_weight = std::clamp(balancer_candidate.static_weight * success_bias, 0.0, 4.0);
        candidates.push_back(std::move(balancer_candidate));
    }
    return candidates;
}

void maybe_prefer_earlier_reset_accounts(
    std::vector<RoutedCredentialCandidate>* candidates,
    const db::DashboardSettingsRecord& settings
) {
    if (candidates == nullptr || !settings.prefer_earlier_reset_accounts || candidates->size() < 2) {
        return;
    }

    std::stable_sort(
        candidates->begin(),
        candidates->end(),
        [](const RoutedCredentialCandidate& lhs, const RoutedCredentialCandidate& rhs) {
            const auto lhs_reset = next_reset_at_ms(lhs);
            const auto rhs_reset = next_reset_at_ms(rhs);
            if (lhs_reset.has_value() && rhs_reset.has_value()) {
                if (*lhs_reset == *rhs_reset) {
                    return lhs.credentials.internal_account_id < rhs.credentials.internal_account_id;
                }
                return *lhs_reset < *rhs_reset;
            }
            if (lhs_reset.has_value()) {
                return true;
            }
            if (rhs_reset.has_value()) {
                return false;
            }
            return lhs.credentials.internal_account_id < rhs.credentials.internal_account_id;
        }
    );
}

const balancer::AccountCandidate* pick_routed_candidate(
    StickyDbState& state,
    const std::vector<balancer::AccountCandidate>& candidates,
    const db::DashboardSettingsRecord& settings
) {
    if (candidates.empty()) {
        return nullptr;
    }

    const auto strategy = normalize_supported_routing_strategy(settings.routing_strategy);
    if (strategy == "round_robin") {
        state.drain_hop_account_id.clear();
        return balancer::pick_round_robin(candidates, state.round_robin_cursor);
    }
    if (strategy == "drain_hop") {
        return pick_drain_hop_candidate(state, candidates);
    }
    state.drain_hop_account_id.clear();
    return state.weighted_round_robin_picker.pick(candidates);
}

bool lock_pool_contains_candidate(
    const std::unordered_set<std::string>& lock_set,
    const RoutedCredentialCandidate& candidate
) {
    if (lock_set.find(candidate.credentials.account_id) != lock_set.end()) {
        return true;
    }
    if (candidate.credentials.internal_account_id > 0) {
        const auto internal_id = std::to_string(candidate.credentials.internal_account_id);
        if (lock_set.find(internal_id) != lock_set.end()) {
            return true;
        }
    }
    return false;
}

} // namespace

StickyAffinityResolution
resolve_sticky_affinity(const std::string& raw_request_body, const openai::HeaderMap& inbound_headers) {
    StickyAffinityResolution resolution;
    const auto request_fields = extract_request_affinity_fields(raw_request_body);
    resolution.sticky_key = request_fields.sticky_key;
    resolution.sticky_kind = normalize_sticky_kind(request_fields.sticky_kind);
    if (resolution.sticky_key.empty()) {
        const auto sticky_from_headers = sticky_key_from_headers(inbound_headers);
        resolution.sticky_key = sticky_from_headers.key;
        resolution.sticky_kind = normalize_sticky_kind(sticky_from_headers.kind);
    }
    resolution.request_model = request_fields.model;
    resolution.account_id = account_from_headers(inbound_headers);
    resolution.from_header = !resolution.account_id.empty();
    if (!resolution.account_id.empty() || resolution.sticky_key.empty()) {
        return resolution;
    }

    auto& state = sticky_db_state();
    auto lock = acquire_sticky_state_lock(state.mutex, "resolve_sticky_affinity");
    sqlite3* db = ensure_db(state);
    if (db == nullptr) {
        return resolution;
    }
    if (!state.sticky_threads_enabled) {
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
    auto lock = acquire_sticky_state_lock(state.mutex, "persist_sticky_affinity");
    sqlite3* db = ensure_db(state);
    if (db == nullptr) {
        return;
    }
    if (!state.sticky_threads_enabled) {
        return;
    }

    const auto now = now_ms();
    (void)db::upsert_proxy_sticky_session(
        db,
        resolution.sticky_key,
        resolution.account_id,
        now,
        state.sticky_ttl_ms,
        resolution.sticky_kind
    );
    maybe_purge_expired(state, db, now);
}

std::string resolve_upstream_stream_transport_setting() {
    auto& state = sticky_db_state();
    auto lock = acquire_sticky_state_lock(state.mutex, "resolve_upstream_stream_transport_setting");
    sqlite3* db = ensure_db(state);
    if (db == nullptr) {
        return "default";
    }
    return normalize_upstream_stream_transport(state.upstream_stream_transport);
}

std::optional<UpstreamAccountCredentials> resolve_upstream_account_credentials(
    const std::string_view preferred_account_id,
    const std::string_view request_model,
    const bool strict_preferred_only
) {
    const auto config = config::load_config();
    const auto db_path = config.db_path.empty() ? std::string("store.db") : config.db_path;
    db::SqlitePool pool(db_path);
    if (!pool.open()) {
        return std::nullopt;
    }
    sqlite3* db = pool.connection();
    if (db == nullptr || !db::ensure_accounts_schema(db)) {
        return std::nullopt;
    }
    recover_expired_account_limit_blocks(db, now_ms());
    const auto settings = db::get_dashboard_settings(db).value_or(db::DashboardSettingsRecord{});
    const bool enforce_strict_preferred = strict_preferred_only;
    const auto locked_account_ids = parse_locked_routing_account_ids(settings.locked_routing_account_ids);
    const bool lock_pool_active = locked_account_ids.size() >= 2;
    const std::unordered_set<std::string> lock_pool_set(locked_account_ids.begin(), locked_account_ids.end());
    auto credentials_in_lock_pool = [&](const UpstreamAccountCredentials& credentials) {
        if (!lock_pool_active) {
            return true;
        }
        if (lock_pool_set.find(credentials.account_id) != lock_pool_set.end()) {
            return true;
        }
        if (credentials.internal_account_id > 0) {
            const auto internal_id = std::to_string(credentials.internal_account_id);
            return lock_pool_set.find(internal_id) != lock_pool_set.end();
        }
        return false;
    };
    auto guard_lock_pool_selection = [&](std::optional<UpstreamAccountCredentials> credentials, std::string_view source) {
        if (!credentials.has_value()) {
            return std::optional<UpstreamAccountCredentials>{};
        }
        if (credentials_in_lock_pool(*credentials)) {
            return credentials;
        }

        core::logging::log_event(
            core::logging::LogLevel::Error,
            "runtime",
            "proxy",
            "lock_pool_route_guard_rejected",
            "source=" + std::string(source) + " account_id=" + credentials->account_id + " lock_pool_size=" +
                std::to_string(locked_account_ids.size())
        );
        emit_runtime_signal(
            "error",
            "lock_pool_route_guard_rejected",
            "lock pool guard rejected out-of-pool account from source=" + std::string(source),
            credentials->account_id
        );
        return std::optional<UpstreamAccountCredentials>{};
    };

    const auto preferred = std::string(core::text::trim_ascii(preferred_account_id));
    auto query_preferred_credentials = [&]() -> std::optional<UpstreamAccountCredentials> {
        if (preferred.empty()) {
            return std::nullopt;
        }
        std::optional<UpstreamAccountCredentials> preferred_credentials = query_exact_account_credentials(db, preferred);
        if (!preferred_credentials.has_value()) {
            if (const auto preferred_internal_id = parse_positive_int64(preferred); preferred_internal_id.has_value()) {
                preferred_credentials = query_internal_account_credentials(db, *preferred_internal_id);
            }
        }
        return preferred_credentials;
    };
    auto preferred_is_allowed = [&](const std::optional<UpstreamAccountCredentials>& preferred_credentials) {
        bool preferred_allowed = !lock_pool_active || lock_pool_set.find(preferred) != lock_pool_set.end();
        if (!preferred_allowed && lock_pool_active && preferred_credentials.has_value()) {
            if (lock_pool_set.find(preferred_credentials->account_id) != lock_pool_set.end()) {
                preferred_allowed = true;
            } else if (preferred_credentials->internal_account_id > 0) {
                const auto preferred_internal_id = std::to_string(preferred_credentials->internal_account_id);
                preferred_allowed = lock_pool_set.find(preferred_internal_id) != lock_pool_set.end();
            }
        }
        return preferred_allowed;
    };
    auto select_preferred_credentials = [&]() -> std::optional<UpstreamAccountCredentials> {
        if (preferred.empty()) {
            return std::nullopt;
        }
        auto preferred_credentials = query_preferred_credentials();
        if (!preferred_is_allowed(preferred_credentials)) {
            return std::nullopt;
        }
        if (auto guarded = guard_lock_pool_selection(std::move(preferred_credentials), "preferred");
            guarded.has_value()) {
            emit_runtime_signal(
                "info",
                "route_account_selected",
                "routing resolved via preferred account",
                guarded->account_id
            );
            return guarded;
        }
        return std::nullopt;
    };

    if (enforce_strict_preferred) {
        return select_preferred_credentials();
    }

    if (!lock_pool_active) {
        if (auto credentials = query_pinned_account_credentials(db); credentials.has_value()) {
            emit_runtime_signal(
                "info",
                "route_account_selected",
                "routing resolved via pinned account",
                credentials->account_id
            );
            return credentials;
        }
    }

    if (!preferred.empty()) {
        if (auto credentials = select_preferred_credentials(); credentials.has_value()) {
            return credentials;
        }
    }

    auto select_strategy_credentials = [&]() -> std::optional<UpstreamAccountCredentials> {
        auto routed_candidates = query_routed_account_candidates(db);
        if (routed_candidates.empty()) {
            return std::nullopt;
        }

        if (locked_account_ids.size() >= 2) {
            routed_candidates.erase(
                std::remove_if(
                    routed_candidates.begin(),
                    routed_candidates.end(),
                    [&lock_pool_set](const RoutedCredentialCandidate& candidate) {
                        return !lock_pool_contains_candidate(lock_pool_set, candidate);
                    }
                ),
                routed_candidates.end()
            );
            if (routed_candidates.empty()) {
                return std::nullopt;
            }
        }

        maybe_prefer_earlier_reset_accounts(&routed_candidates, settings);
        auto balancer_candidates = to_balancer_candidates(routed_candidates, request_model, settings);
        if (balancer_candidates.empty()) {
            return std::nullopt;
        }

        std::size_t index = std::numeric_limits<std::size_t>::max();
        {
            auto& state = sticky_db_state();
            auto lock = acquire_sticky_state_lock(state.mutex, "resolve_upstream_account_strategy_pick");
            const auto* selected = pick_routed_candidate(state, balancer_candidates, settings);
            if (selected == nullptr) {
                return std::nullopt;
            }
            const auto* begin = balancer_candidates.data();
            const auto* end = begin + balancer_candidates.size();
            if (selected < begin || selected >= end) {
                return std::nullopt;
            }
            index = static_cast<std::size_t>(selected - begin);
        }

        if (index >= routed_candidates.size()) {
            return std::nullopt;
        }
        return routed_candidates[index].credentials;
    };

    if (auto credentials = guard_lock_pool_selection(select_strategy_credentials(), "strategy");
        credentials.has_value()) {
        emit_runtime_signal(
            "info",
            "route_account_selected",
            "routing resolved via strategy account",
            credentials->account_id
        );
        return credentials;
    }

    if (lock_pool_active) {
        return std::nullopt;
    }

    if (auto credentials = guard_lock_pool_selection(query_latest_active_account_credentials(db), "fallback_active");
        credentials.has_value()) {
        emit_runtime_signal(
            "info",
            "route_account_selected",
            "routing resolved via fallback active account",
            credentials->account_id
        );
        return credentials;
    }
    return std::nullopt;
}

std::optional<UpstreamAccountCredentials> refresh_upstream_account_credentials(const std::string_view account_id) {
    if (account_id.empty()) {
        return std::nullopt;
    }

    // Refresh can perform outbound OAuth I/O. Do not hold the global sticky-affinity mutex while doing it,
    // otherwise all transports can stall behind a single slow refresh.
    const auto config = config::load_config();
    const auto db_path = config.db_path.empty() ? std::string("store.db") : config.db_path;
    db::SqlitePool pool(db_path);
    if (!pool.open()) {
        return std::nullopt;
    }
    sqlite3* db = pool.connection();
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

bool upstream_account_record_exists(const std::string_view account_id) {
    const auto normalized_id = std::string(core::text::trim_ascii(account_id));
    if (normalized_id.empty()) {
        return false;
    }
    const auto config = config::load_config();
    const auto db_path = config.db_path.empty() ? std::string("store.db") : config.db_path;
    db::SqlitePool pool(db_path);
    if (!pool.open()) {
        return false;
    }
    sqlite3* db = pool.connection();
    if (db == nullptr || !db::ensure_accounts_schema(db)) {
        return false;
    }

    if (account_record_exists_by_chatgpt_id(db, normalized_id)) {
        pool.close();
        return true;
    }
    if (const auto internal_id = parse_positive_int64(normalized_id); internal_id.has_value()) {
        const bool exists = account_record_exists_by_internal_id(db, *internal_id);
        pool.close();
        return exists;
    }
    pool.close();
    return false;
}

bool mark_upstream_account_unusable(const std::string_view account_id) {
    const auto normalized_id = std::string(core::text::trim_ascii(account_id));
    if (normalized_id.empty()) {
        return false;
    }

    auto& state = sticky_db_state();
    auto lock = acquire_sticky_state_lock(state.mutex, "mark_upstream_account_unusable");
    sqlite3* db = ensure_db(state);
    if (db == nullptr || !db::ensure_accounts_schema(db)) {
        return false;
    }

    const auto purged_count = db::purge_proxy_sticky_sessions_for_account(db, normalized_id);
    const auto purged_continuity_count = db::purge_proxy_response_continuity_for_account(db, normalized_id);
    const bool marked_unusable = mark_account_unusable_by_chatgpt_id(db, normalized_id);
    const bool unpinned = db::clear_account_routing_pin_by_chatgpt_account_id(db, normalized_id);
    if (state.drain_hop_account_id == normalized_id) {
        state.drain_hop_account_id.clear();
    }
    const bool changed = marked_unusable || unpinned || purged_count > 0 || purged_continuity_count > 0;
    if (changed) {
        emit_runtime_signal(
            "error",
            "account_unusable",
            "account marked unusable after upstream deactivation",
            normalized_id
        );
    }

    return changed;
}

bool mark_upstream_account_exhausted(const std::string_view account_id, const std::string_view status) {
    const auto normalized_id = std::string(core::text::trim_ascii(account_id));
    if (normalized_id.empty()) {
        return false;
    }

    auto& state = sticky_db_state();
    auto lock = acquire_sticky_state_lock(state.mutex, "mark_upstream_account_exhausted");
    sqlite3* db = ensure_db(state);
    if (db == nullptr || !db::ensure_accounts_schema(db)) {
        return false;
    }

    const auto purged_count = db::purge_proxy_sticky_sessions_for_account(db, normalized_id);
    const auto purged_continuity_count = db::purge_proxy_response_continuity_for_account(db, normalized_id);
    const bool marked_exhausted = mark_account_exhausted_by_chatgpt_id(db, normalized_id, status);
    const bool unpinned = db::clear_account_routing_pin_by_chatgpt_account_id(db, normalized_id);
    if (state.drain_hop_account_id == normalized_id) {
        state.drain_hop_account_id.clear();
    }
    const bool changed = marked_exhausted || unpinned || purged_count > 0 || purged_continuity_count > 0;
    if (changed) {
        emit_runtime_signal(
            "warn",
            "account_exhausted",
            "account marked exhausted by upstream response status=" + std::string(status),
            normalized_id
        );
    }

    return changed;
}

bool account_is_in_active_lock_pool(const std::string_view account_id) {
    const auto normalized_id = std::string(core::text::trim_ascii(account_id));
    if (normalized_id.empty()) {
        return false;
    }

    auto& state = sticky_db_state();
    auto lock = acquire_sticky_state_lock(state.mutex, "account_is_in_active_lock_pool");
    sqlite3* db = ensure_db(state);
    if (db == nullptr) {
        return false;
    }

    const auto settings = db::get_dashboard_settings(db).value_or(db::DashboardSettingsRecord{});
    const auto locked_account_ids = parse_locked_routing_account_ids(settings.locked_routing_account_ids);
    if (locked_account_ids.size() < 2) {
        return false;
    }
    return std::find(locked_account_ids.begin(), locked_account_ids.end(), normalized_id) != locked_account_ids.end();
}

} // namespace tightrope::proxy::session
