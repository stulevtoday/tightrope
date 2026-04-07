#include "internal/token_refresh_scheduler.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "controllers/controller_db.h"
#include "logging/logger.h"
#include "oauth/token_refresh.h"
#include "repositories/account_repo.h"
#include "text/ascii.h"
#include "time/clock.h"

namespace tightrope::server::internal::token_refresh {

namespace {

struct RefreshCandidate {
    std::int64_t account_id = 0;
    std::string chatgpt_account_id;
    std::string label;
    std::optional<std::int64_t> last_success_at_ms;
    std::optional<std::int64_t> next_due_at_ms;
};

core::time::Clock& runtime_clock() {
    static core::time::SystemClock clock;
    return clock;
}

std::int64_t now_ms() {
    return runtime_clock().unix_ms_now();
}

std::optional<std::int64_t> parse_positive_i64(const char* value) {
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    std::int64_t parsed = 0;
    const auto* begin = value;
    const auto* end = value + std::char_traits<char>::length(value);
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed <= 0) {
        return std::nullopt;
    }
    return parsed;
}

std::int64_t env_positive_i64(const char* key, const std::int64_t fallback) {
    if (key == nullptr) {
        return fallback;
    }
    const auto parsed = parse_positive_i64(std::getenv(key));
    return parsed.value_or(fallback);
}

SchedulerConfig sanitize_config(const SchedulerConfig& raw) {
    SchedulerConfig config = raw;
    config.startup_delay_ms = std::max<std::int64_t>(0, config.startup_delay_ms);
    config.startup_stale_after_ms = std::max<std::int64_t>(1, config.startup_stale_after_ms);
    config.periodic_check_interval_ms = std::max<std::int64_t>(1, config.periodic_check_interval_ms);
    config.success_cycle_ms = std::max<std::int64_t>(1, config.success_cycle_ms);
    config.failure_retry_ms = std::max<std::int64_t>(1, config.failure_retry_ms);
    config.stop_poll_interval_ms = std::max<std::int64_t>(1, config.stop_poll_interval_ms);
    return config;
}

std::optional<std::int64_t> optional_i64(sqlite3_stmt* stmt, const int index) {
    if (stmt == nullptr || sqlite3_column_type(stmt, index) == SQLITE_NULL) {
        return std::nullopt;
    }
    return sqlite3_column_int64(stmt, index);
}

std::string sqlite_text_or_empty(sqlite3_stmt* stmt, const int index) {
    if (stmt == nullptr) {
        return {};
    }
    const auto* text = sqlite3_column_text(stmt, index);
    if (text == nullptr) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(text));
}

std::vector<RefreshCandidate> load_refresh_candidates(sqlite3* db) {
    std::vector<RefreshCandidate> candidates;
    if (db == nullptr || !db::ensure_accounts_schema(db)) {
        return candidates;
    }

    constexpr const char* kSql = R"SQL(
SELECT
    id,
    chatgpt_account_id,
    email,
    token_refresh_last_success_at_ms,
    token_refresh_next_due_at_ms,
    CAST(strftime('%s', last_refresh) AS INTEGER)
FROM accounts
WHERE status = 'active'
  AND chatgpt_account_id IS NOT NULL
  AND trim(chatgpt_account_id) != ''
  AND refresh_token_encrypted IS NOT NULL
  AND length(refresh_token_encrypted) > 0
ORDER BY id ASC;
)SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
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
        RefreshCandidate candidate;
        candidate.account_id = sqlite3_column_int64(stmt, 0);
        candidate.chatgpt_account_id = core::text::trim_ascii(sqlite_text_or_empty(stmt, 1));
        candidate.label = core::text::trim_ascii(sqlite_text_or_empty(stmt, 2));
        candidate.next_due_at_ms = optional_i64(stmt, 4);

        auto last_success_at_ms = optional_i64(stmt, 3);
        if (!last_success_at_ms.has_value()) {
            const auto last_refresh_epoch_s = optional_i64(stmt, 5);
            if (last_refresh_epoch_s.has_value() && *last_refresh_epoch_s > 0) {
                last_success_at_ms = *last_refresh_epoch_s * 1000;
            }
        }
        candidate.last_success_at_ms = last_success_at_ms;

        if (candidate.account_id <= 0 || candidate.chatgpt_account_id.empty()) {
            continue;
        }
        if (candidate.label.empty()) {
            candidate.label = std::to_string(candidate.account_id);
        }
        candidates.push_back(std::move(candidate));
    }

    finalize();
    return candidates;
}

bool due_for_startup(const RefreshCandidate& candidate, const std::int64_t now, const SchedulerConfig& config) {
    if (!candidate.last_success_at_ms.has_value()) {
        return true;
    }
    if (now <= *candidate.last_success_at_ms) {
        return false;
    }
    return (now - *candidate.last_success_at_ms) >= config.startup_stale_after_ms;
}

bool due_for_periodic(const RefreshCandidate& candidate, const std::int64_t now, const SchedulerConfig& config) {
    if (candidate.next_due_at_ms.has_value()) {
        return *candidate.next_due_at_ms <= now;
    }
    if (!candidate.last_success_at_ms.has_value()) {
        return true;
    }
    if (now <= *candidate.last_success_at_ms) {
        return false;
    }
    return (now - *candidate.last_success_at_ms) >= config.success_cycle_ms;
}

std::string refresh_error_detail(const auth::oauth::RefreshAccessTokenResult& refresh) {
    auto code = std::string(core::text::trim_ascii(refresh.error_code));
    auto message = std::string(core::text::trim_ascii(refresh.error_message));
    if (code.empty() && message.empty()) {
        return "token refresh failed";
    }
    if (code.empty()) {
        return message;
    }
    if (message.empty()) {
        return code;
    }
    return code + ": " + message;
}

bool mark_attention(
    sqlite3* db,
    const std::int64_t account_id,
    const std::int64_t next_due_at_ms,
    const std::string_view error_detail
) {
    if (db == nullptr || account_id <= 0) {
        return false;
    }
    constexpr const char* kSql = R"SQL(
UPDATE accounts
SET token_refresh_needs_attention = 1,
    token_refresh_last_error = ?1,
    token_refresh_next_due_at_ms = ?2,
    updated_at = datetime('now')
WHERE id = ?3;
)SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
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

    const auto error = std::string(core::text::trim_ascii(error_detail));
    const auto bind_ok = sqlite3_bind_text(stmt, 1, error.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
                         sqlite3_bind_int64(stmt, 2, next_due_at_ms) == SQLITE_OK &&
                         sqlite3_bind_int64(stmt, 3, account_id) == SQLITE_OK;
    if (!bind_ok) {
        finalize();
        return false;
    }
    const auto rc = sqlite3_step(stmt);
    const auto changes = sqlite3_changes(db);
    finalize();
    return rc == SQLITE_DONE && changes > 0;
}

void run_sweep_with_fresh_connection(const std::int64_t now, const SchedulerConfig& config, const SweepMode mode) {
    auto handle = controllers::open_controller_db(nullptr);
    if (handle.db == nullptr) {
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "token_refresh",
            "scheduler_db_unavailable"
        );
        return;
    }

    const auto result = run_sweep_once(handle.db, now, config, mode);
    if (result.due > 0 || result.failed > 0) {
        core::logging::log_event(
            result.failed > 0 ? core::logging::LogLevel::Warning : core::logging::LogLevel::Info,
            "runtime",
            "token_refresh",
            "scheduler_sweep",
            "mode=" + std::string(mode == SweepMode::startup ? "startup" : "periodic") + " scanned=" +
                std::to_string(result.scanned) + " due=" + std::to_string(result.due) + " refreshed=" +
                std::to_string(result.refreshed) + " failed=" + std::to_string(result.failed)
        );
    }
}

} // namespace

SchedulerConfig load_scheduler_config_from_env() noexcept {
    auto config = SchedulerConfig{};
    config.startup_delay_ms = env_positive_i64("TIGHTROPE_TOKEN_REFRESH_STARTUP_DELAY_MS", config.startup_delay_ms);
    config.startup_stale_after_ms =
        env_positive_i64("TIGHTROPE_TOKEN_REFRESH_STARTUP_STALE_AFTER_MS", config.startup_stale_after_ms);
    config.periodic_check_interval_ms = env_positive_i64(
        "TIGHTROPE_TOKEN_REFRESH_CHECK_INTERVAL_MS",
        config.periodic_check_interval_ms
    );
    config.success_cycle_ms = env_positive_i64("TIGHTROPE_TOKEN_REFRESH_SUCCESS_CYCLE_MS", config.success_cycle_ms);
    config.failure_retry_ms = env_positive_i64("TIGHTROPE_TOKEN_REFRESH_FAILURE_RETRY_MS", config.failure_retry_ms);
    config.stop_poll_interval_ms = env_positive_i64("TIGHTROPE_TOKEN_REFRESH_STOP_POLL_MS", config.stop_poll_interval_ms);
    return sanitize_config(config);
}

SweepResult run_sweep_once(sqlite3* db, const std::int64_t now, const SchedulerConfig& raw_config, const SweepMode mode)
    noexcept {
    SweepResult result;
    if (db == nullptr) {
        return result;
    }
    const auto config = sanitize_config(raw_config);

    const auto candidates = load_refresh_candidates(db);
    result.scanned = candidates.size();

    for (const auto& candidate : candidates) {
        const bool due = mode == SweepMode::startup ? due_for_startup(candidate, now, config)
                                                    : due_for_periodic(candidate, now, config);
        if (!due) {
            continue;
        }
        ++result.due;

        const auto refresh = auth::oauth::refresh_access_token_for_account(db, candidate.chatgpt_account_id);
        if (refresh.refreshed) {
            ++result.refreshed;
            continue;
        }

        ++result.failed;
        const auto detail = refresh_error_detail(refresh);
        const auto next_due_at_ms = now + config.failure_retry_ms;
        const auto marked = mark_attention(db, candidate.account_id, next_due_at_ms, detail);
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "token_refresh",
            "scheduler_refresh_failed",
            "account_id=" + std::to_string(candidate.account_id) + " label=" + candidate.label + " marked_attention=" +
                (marked ? "true" : "false") + " error=" + detail
        );
    }

    return result;
}

void run_scheduler_loop(const std::stop_token& stop_token, const SchedulerConfig& raw_config) noexcept {
    const auto config = sanitize_config(raw_config);
    std::int64_t startup_slept_ms = 0;
    while (startup_slept_ms < config.startup_delay_ms && !stop_token.stop_requested()) {
        const auto step = std::min(config.stop_poll_interval_ms, config.startup_delay_ms - startup_slept_ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(step));
        startup_slept_ms += step;
    }
    if (!stop_token.stop_requested()) {
        run_sweep_with_fresh_connection(now_ms(), config, SweepMode::startup);
    }

    while (!stop_token.stop_requested()) {
        std::int64_t slept_ms = 0;
        while (slept_ms < config.periodic_check_interval_ms && !stop_token.stop_requested()) {
            const auto step = std::min(config.stop_poll_interval_ms, config.periodic_check_interval_ms - slept_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(step));
            slept_ms += step;
        }
        if (stop_token.stop_requested()) {
            break;
        }
        run_sweep_with_fresh_connection(now_ms(), config, SweepMode::periodic);
    }
}

} // namespace tightrope::server::internal::token_refresh
