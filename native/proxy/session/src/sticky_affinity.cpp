#include "sticky_affinity.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glaze/glaze.hpp>

#include "config_loader.h"
#include "text/ascii.h"
#include "eligibility.h"
#include "scorer.h"
#include "connection/sqlite_pool.h"
#include "logging/logger.h"
#include "oauth/token_refresh.h"
#include "repositories/account_repo.h"
#include "repositories/settings_repo.h"
#include "repositories/session_repo.h"
#include "strategies/cost_aware.h"
#include "strategies/deadline_aware.h"
#include "strategies/headroom.h"
#include "strategies/latency_ewma.h"
#include "strategies/least_outstanding.h"
#include "strategies/power_of_two.h"
#include "strategies/round_robin.h"
#include "strategies/success_rate.h"
#include "strategies/weighted_round_robin.h"

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
SELECT id, chatgpt_account_id, access_token_encrypted
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
SELECT id, chatgpt_account_id, access_token_encrypted
FROM accounts
WHERE status = 'active'
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
       plan_type
FROM accounts
WHERE status = 'active'
  AND chatgpt_account_id IS NOT NULL
  AND trim(chatgpt_account_id) != ''
  AND access_token_encrypted IS NOT NULL
  AND length(access_token_encrypted) > 0
ORDER BY updated_at DESC, id DESC;
)SQL";

struct RoutedCredentialCandidate {
    UpstreamAccountCredentials credentials;
    std::optional<int> quota_primary_percent;
    std::optional<int> quota_secondary_percent;
    std::optional<std::string> plan_type;
};

struct StickyDbState {
    std::mutex mutex;
    std::string db_path;
    std::unique_ptr<db::SqlitePool> pool;
    bool schema_ready = false;
    std::int64_t last_cleanup_ms = 0;
    std::size_t round_robin_cursor = 0;
    balancer::PowerOfTwoPicker power_of_two_picker{};
    balancer::WeightedRoundRobinPicker weighted_round_robin_picker{};
    balancer::SuccessRateWeightedPicker success_rate_weighted_picker{};
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
        state.round_robin_cursor = 0;
        state.power_of_two_picker = balancer::PowerOfTwoPicker{};
        state.weighted_round_robin_picker = balancer::WeightedRoundRobinPicker{};
        state.success_rate_weighted_picker = balancer::SuccessRateWeightedPicker{};
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
    const auto internal_account_id = sqlite3_column_int64(stmt, 0);
    const auto* account_raw = sqlite3_column_text(stmt, 1);
    const auto* token_raw = sqlite3_column_text(stmt, 2);
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
        .internal_account_id = internal_account_id,
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

std::optional<int> optional_int_column(sqlite3_stmt* stmt, const int index) {
    if (stmt == nullptr || sqlite3_column_type(stmt, index) == SQLITE_NULL) {
        return std::nullopt;
    }
    return sqlite3_column_int(stmt, index);
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

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kFindRoutedCredentialSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
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
        auto access_token = std::string(core::text::trim_ascii(reinterpret_cast<const char*>(token_raw)));
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
        candidates.push_back(std::move(candidate));
    }

    finalize();
    return candidates;
}

double usage_ratio_from_percent(const std::optional<int> percent) {
    if (!percent.has_value()) {
        return 0.0;
    }
    const auto clamped = std::clamp(*percent, 0, 100);
    return static_cast<double>(clamped) / 100.0;
}

double normalized_plan_cost(const std::optional<std::string>& plan_type) {
    if (!plan_type.has_value()) {
        return 0.5;
    }
    const auto normalized = core::text::to_lower_ascii(core::text::trim_ascii(*plan_type));
    if (normalized.empty()) {
        return 0.5;
    }
    if (normalized == "free") {
        return 0.1;
    }
    if (normalized == "plus") {
        return 0.35;
    }
    if (normalized == "pro") {
        return 0.5;
    }
    if (normalized == "team") {
        return 0.65;
    }
    if (normalized == "business") {
        return 0.8;
    }
    if (normalized == "enterprise") {
        return 1.0;
    }
    return 0.6;
}

balancer::HeadroomWeights headroom_weights_from_settings(const db::DashboardSettingsRecord& settings) {
    return balancer::HeadroomWeights{
        .primary = std::max(0.0, settings.routing_headroom_weight_primary),
        .secondary = std::max(0.0, settings.routing_headroom_weight_secondary),
    };
}

balancer::ScoreWeights score_weights_from_settings(const db::DashboardSettingsRecord& settings) {
    return balancer::ScoreWeights{
        .headroom = std::max(0.0, settings.routing_score_delta),
        .success_rate = std::max(0.0, settings.routing_score_gamma),
        .latency_penalty = std::max(0.0, settings.routing_score_beta),
        .outstanding_penalty = std::max(0.0, settings.routing_score_alpha),
    };
}

std::vector<balancer::AccountCandidate> to_balancer_candidates(
    const std::vector<RoutedCredentialCandidate>& routed_candidates
) {
    std::vector<balancer::AccountCandidate> candidates;
    candidates.reserve(routed_candidates.size());
    for (const auto& candidate : routed_candidates) {
        balancer::AccountCandidate balancer_candidate;
        balancer_candidate.id = candidate.credentials.account_id;
        balancer_candidate.active = true;
        balancer_candidate.healthy = true;
        balancer_candidate.enabled = true;
        balancer_candidate.usage_ratio = usage_ratio_from_percent(candidate.quota_primary_percent);
        if (candidate.quota_secondary_percent.has_value()) {
            balancer_candidate.secondary_usage_ratio = usage_ratio_from_percent(candidate.quota_secondary_percent);
        }
        balancer_candidate.normalized_cost = normalized_plan_cost(candidate.plan_type);
        candidates.push_back(std::move(balancer_candidate));
    }
    return candidates;
}

const balancer::AccountCandidate* pick_routed_candidate(
    StickyDbState& state,
    const std::vector<balancer::AccountCandidate>& candidates,
    const db::DashboardSettingsRecord& settings
) {
    if (candidates.empty()) {
        return nullptr;
    }

    const auto strategy = core::text::to_lower_ascii(core::text::trim_ascii(settings.routing_strategy));
    const auto headroom_weights = headroom_weights_from_settings(settings);
    const auto score_weights = score_weights_from_settings(settings);
    if (strategy == "round_robin") {
        return balancer::pick_round_robin(candidates, state.round_robin_cursor);
    }
    if (strategy == "weighted_round_robin") {
        return state.weighted_round_robin_picker.pick(candidates);
    }
    if (strategy == "least_outstanding_requests" || strategy == "least_outstanding") {
        return balancer::pick_least_outstanding(candidates);
    }
    if (strategy == "latency_ewma") {
        return balancer::pick_lowest_latency_ewma(candidates);
    }
    if (strategy == "success_rate_weighted") {
        return state.success_rate_weighted_picker.pick(candidates, {}, std::max(0.1, settings.routing_success_rate_rho), 1e-6);
    }
    if (strategy == "cost_aware") {
        balancer::CostAwareGuardrails guardrails{};
        guardrails.headroom_weights = headroom_weights;
        return balancer::pick_cost_aware(candidates, {}, guardrails);
    }
    if (strategy == "deadline_aware") {
        balancer::DeadlineAwareOptions options{};
        options.base_weights = score_weights;
        return balancer::pick_deadline_aware(candidates, {}, options);
    }
    if (strategy == "power_of_two_choices") {
        return state.power_of_two_picker.pick(candidates, {}, score_weights);
    }
    if (strategy == "usage_weighted" || strategy == "headroom_score") {
        return balancer::pick_headroom_score(candidates, {}, headroom_weights);
    }
    return balancer::pick_headroom_score(candidates, {}, headroom_weights);
}

std::optional<UpstreamAccountCredentials> query_routing_strategy_account_credentials(StickyDbState& state, sqlite3* db) {
    auto routed_candidates = query_routed_account_candidates(db);
    if (routed_candidates.empty()) {
        return std::nullopt;
    }

    const auto settings = db::get_dashboard_settings(db).value_or(db::DashboardSettingsRecord{});
    auto balancer_candidates = to_balancer_candidates(routed_candidates);
    const auto* selected = pick_routed_candidate(state, balancer_candidates, settings);
    if (selected == nullptr || balancer_candidates.empty()) {
        return std::nullopt;
    }

    const auto* begin = balancer_candidates.data();
    const auto* end = begin + balancer_candidates.size();
    if (selected < begin || selected >= end) {
        return std::nullopt;
    }
    const auto index = static_cast<std::size_t>(selected - begin);
    if (index >= routed_candidates.size()) {
        return std::nullopt;
    }
    return routed_candidates[index].credentials;
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

    if (auto credentials = query_routing_strategy_account_credentials(state, db); credentials.has_value()) {
        return credentials;
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
