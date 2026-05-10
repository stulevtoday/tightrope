#include "accounts_controller.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <optional>
#include <unordered_map>

#include "controller_db.h"
#include "account_traffic.h"
#include "oauth/token_refresh.h"
#include "repositories/account_repo.h"
#include "repositories/request_log_repo.h"
#include "text/ascii.h"
#include "time/clock.h"
#include "token_store.h"
#include "usage_fetcher.h"

namespace tightrope::server::controllers {

namespace {

std::optional<std::int64_t> parse_account_id(const std::string_view value) {
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

struct AccountUsageCostSnapshot {
    std::uint64_t requests_24h = 0;
    double total_cost_24h_usd = 0.0;
    double cost_norm = 0.0;
};

using AccountUsageCostMap = std::unordered_map<std::int64_t, AccountUsageCostSnapshot>;

std::string normalize_account_status(std::string_view status);

AccountUsageCostMap load_account_usage_cost_map(sqlite3* db) {
    AccountUsageCostMap usage_by_account;
    const auto aggregates = db::list_account_request_cost_aggregates(db, 24);
    if (aggregates.empty()) {
        return usage_by_account;
    }

    double max_total_cost = 0.0;
    for (const auto& aggregate : aggregates) {
        max_total_cost = std::max(max_total_cost, std::max(0.0, aggregate.total_cost));
    }

    for (const auto& aggregate : aggregates) {
        const auto clamped_cost = std::max(0.0, aggregate.total_cost);
        const auto norm = max_total_cost > 0.0 ? std::clamp(clamped_cost / max_total_cost, 0.0, 1.0) : 0.0;
        usage_by_account[aggregate.account_id] = AccountUsageCostSnapshot{
            .requests_24h = aggregate.requests,
            .total_cost_24h_usd = clamped_cost,
            .cost_norm = norm,
        };
    }
    return usage_by_account;
}

AccountPayload to_payload(const db::AccountRecord& record, const AccountUsageCostMap* usage_cost_map = nullptr) {
    std::optional<std::uint64_t> requests_24h;
    std::optional<double> total_cost_24h_usd;
    std::optional<double> cost_norm;
    if (usage_cost_map != nullptr) {
        if (const auto it = usage_cost_map->find(record.id); it != usage_cost_map->end()) {
            requests_24h = it->second.requests_24h;
            total_cost_24h_usd = it->second.total_cost_24h_usd;
            cost_norm = it->second.cost_norm;
        }
    }
    return {
        .account_id = std::to_string(record.id),
        .email = record.email,
        .provider = record.provider,
        .status = normalize_account_status(record.status),
        .pinned = record.routing_pinned,
        .plan_type = record.plan_type,
        .quota_primary_percent = record.quota_primary_percent,
        .quota_secondary_percent = record.quota_secondary_percent,
        .quota_primary_window_seconds = record.quota_primary_limit_window_seconds,
        .quota_secondary_window_seconds = record.quota_secondary_limit_window_seconds,
        .quota_primary_reset_at_ms = record.quota_primary_reset_at_ms,
        .quota_secondary_reset_at_ms = record.quota_secondary_reset_at_ms,
        .requests_24h = requests_24h,
        .total_cost_24h_usd = total_cost_24h_usd,
        .cost_norm = cost_norm,
    };
}

AccountTokenMigrationPayload to_payload(const db::TokenStorageMigrationResult& result, const bool dry_run) {
    return {
        .scanned_accounts = result.scanned_accounts,
        .plaintext_accounts = result.plaintext_accounts,
        .plaintext_tokens = result.plaintext_tokens,
        .migrated_accounts = result.migrated_accounts,
        .migrated_tokens = result.migrated_tokens,
        .failed_accounts = result.failed_accounts,
        .dry_run = dry_run,
        .strict_mode_enabled = !::tightrope::auth::crypto::token_storage_plaintext_allowed(),
        .migrate_plaintext_on_read_enabled = ::tightrope::auth::crypto::token_storage_migrate_plaintext_on_read_enabled(),
    };
}

AccountMutationResponse not_found() {
    return {
        .status = 404,
        .code = "account_not_found",
        .message = "Account not found",
    };
}

AccountMutationResponse account_not_active() {
    return {
        .status = 400,
        .code = "account_not_active",
        .message = "Only active accounts can be pinned",
    };
}

std::optional<int> normalized_percent(const std::optional<int> value) {
    if (!value.has_value()) {
        return std::nullopt;
    }
    return std::clamp(*value, 0, 100);
}

std::optional<int> normalized_window_seconds(const std::optional<int> value) {
    if (!value.has_value() || *value <= 0) {
        return std::nullopt;
    }
    return *value;
}

std::optional<std::int64_t>
normalized_reset_at_ms(const usage::UsageWindowSnapshot& window, const std::int64_t captured_now_ms) {
    if (window.reset_at.has_value() && *window.reset_at > 0) {
        // Upstream may emit reset timestamps in seconds or milliseconds.
        constexpr std::int64_t kMaxUnixSecondsBeforeMsLikely = 10'000'000'000LL;
        if (*window.reset_at > kMaxUnixSecondsBeforeMsLikely) {
            return *window.reset_at;
        }
        return *window.reset_at * 1000;
    }
    if (!window.reset_after_seconds.has_value() || *window.reset_after_seconds <= 0) {
        return std::nullopt;
    }

    const auto reset_after_ms = static_cast<std::int64_t>(*window.reset_after_seconds) * 1000;
    if (captured_now_ms > (std::numeric_limits<std::int64_t>::max() - reset_after_ms)) {
        return std::nullopt;
    }
    return captured_now_ms + reset_after_ms;
}

std::string normalize_account_status(const std::string_view status) {
    auto normalized = core::text::to_lower_ascii(core::text::trim_ascii(status));
    if (normalized == "quota_exceeded") {
        return "quota_blocked";
    }
    return normalized;
}

bool is_hard_blocked_account_status(const std::string_view status) {
    return status == "paused" || status == "deactivated";
}

bool is_recoverable_account_status(const std::string_view status) {
    return status.empty() || status == "active" || status == "rate_limited" || status == "quota_blocked";
}

bool usage_validation_indicates_deactivated(const usage::UsageValidationResult& validation) {
    if (validation.status_code != 401 && validation.status_code != 403) {
        return false;
    }

    const auto normalized_error_code = core::text::to_lower_ascii(core::text::trim_ascii(validation.error_code));
    if (normalized_error_code == "account_deactivated") {
        return true;
    }

    const auto normalized_message = core::text::to_lower_ascii(validation.message);
    return normalized_message.find("deactivat") != std::string::npos;
}

bool usage_validation_requires_token_refresh(const usage::UsageValidationResult& validation) {
    if (validation.status_code != 401) {
        return false;
    }
    if (usage_validation_indicates_deactivated(validation)) {
        return false;
    }
    return true;
}

std::string usage_refresh_failure_message(
    const usage::UsageValidationResult& validation,
    const std::string_view account_label
) {
    std::string message = "Failed to fetch usage telemetry from provider";
    const auto normalized_account_label = std::string(core::text::trim_ascii(account_label));
    if (!normalized_account_label.empty()) {
        message += " for ";
        message += normalized_account_label;
    }
    if (validation.status_code > 0) {
        message += " (HTTP ";
        message += std::to_string(validation.status_code);
        message += ")";
    }

    const auto error_code = std::string(core::text::trim_ascii(validation.error_code));
    const auto detail = std::string(core::text::trim_ascii(validation.message));
    if (error_code.empty() && detail.empty()) {
        if (validation.status_code <= 0) {
            message += ": no upstream status returned";
        }
        return message;
    }

    message += ": ";
    if (!error_code.empty()) {
        message += "code=";
        message += error_code;
    }
    if (!detail.empty()) {
        if (!error_code.empty()) {
            message += ", ";
        }
        message += detail;
    }
    return message;
}

std::string token_refresh_failure_message(
    const auth::oauth::RefreshAccessTokenResult& refresh,
    const std::string_view account_label
) {
    std::string message = "Failed to refresh account token";
    const auto normalized_account_label = std::string(core::text::trim_ascii(account_label));
    if (!normalized_account_label.empty()) {
        message += " for ";
        message += normalized_account_label;
    }

    const auto error_code = std::string(core::text::trim_ascii(refresh.error_code));
    const auto detail = std::string(core::text::trim_ascii(refresh.error_message));
    if (error_code.empty() && detail.empty()) {
        return message;
    }

    message += ": ";
    if (!error_code.empty()) {
        message += "code=";
        message += error_code;
    }
    if (!detail.empty()) {
        if (!error_code.empty()) {
            message += ", ";
        }
        message += detail;
    }
    return message;
}

bool is_free_plan_type(const std::string_view plan_type) {
    const auto normalized = core::text::to_lower_ascii(core::text::trim_ascii(plan_type));
    if (normalized.empty()) {
        return false;
    }
    return normalized.find("free") != std::string::npos || normalized.find("guest") != std::string::npos;
}

std::string reconcile_account_status_from_usage(
    const std::string_view current_status,
    const std::optional<int> quota_primary_percent,
    const std::optional<int> quota_secondary_percent,
    const std::string_view plan_type
) {
    auto status = normalize_account_status(current_status);
    if (is_hard_blocked_account_status(status)) {
        return status;
    }
    if (!is_recoverable_account_status(status)) {
        return status;
    }
    if (status.empty()) {
        status = "active";
    }

    if (is_free_plan_type(plan_type)) {
        const auto free_weekly_percent = quota_primary_percent.has_value() ? quota_primary_percent : quota_secondary_percent;
        if (free_weekly_percent.has_value()) {
            if (*free_weekly_percent >= 100) {
                return "quota_blocked";
            }
            if (status == "quota_blocked" || status == "rate_limited") {
                status = "active";
            }
        }
        return status;
    }

    if (quota_secondary_percent.has_value()) {
        if (*quota_secondary_percent >= 100) {
            return "quota_blocked";
        }
        if (status == "quota_blocked") {
            status = "active";
        }
    }

    if (quota_primary_percent.has_value()) {
        if (*quota_primary_percent >= 100) {
            return "rate_limited";
        }
        if (status == "rate_limited") {
            status = "active";
        }
    }

    return status;
}

std::optional<db::AccountRecord> find_account_record(sqlite3* database, const std::int64_t account_id) {
    const auto records = db::list_accounts(database);
    const auto it = std::find_if(records.begin(), records.end(), [account_id](const db::AccountRecord& record) {
        return record.id == account_id;
    });
    if (it == records.end()) {
        return std::nullopt;
    }
    return *it;
}

core::time::Clock& runtime_clock() {
    static core::time::SystemClock clock;
    return clock;
}

std::int64_t now_ms() {
    return runtime_clock().unix_ms_now();
}

} // namespace

AccountsResponse list_accounts(sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }

    const auto usage_cost_map = load_account_usage_cost_map(handle.db);
    AccountsResponse response{.status = 200};
    for (const auto& record : db::list_accounts(handle.db)) {
        response.accounts.push_back(to_payload(record, &usage_cost_map));
    }
    return response;
}

AccountMutationResponse import_account(const std::string_view email, const std::string_view provider, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }

    const auto created = db::import_account(handle.db, email, provider);
    if (!created.has_value()) {
        return {
            .status = 400,
            .code = "invalid_account_import",
            .message = "Invalid account payload",
        };
    }
    return {
        .status = 201,
        .account = to_payload(*created),
    };
}

AccountMutationResponse import_account_with_tokens(const std::string_view email, const std::string_view provider, const std::string_view access_token, const std::string_view refresh_token, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }

    db::OauthAccountUpsert upsert;
    upsert.email = std::string(email);
    upsert.provider = std::string(provider);
    upsert.access_token_encrypted = std::string(access_token);
    upsert.refresh_token_encrypted = std::string(refresh_token);

    const auto result = db::upsert_oauth_account(handle.db, upsert);
    if (!result.has_value()) {
        return {
            .status = 400,
            .code = "invalid_account_import",
            .message = "Failed to import account with tokens",
        };
    }
    return {
        .status = 201,
        .account = to_payload(*result),
    };
}

AccountMutationResponse pin_account(const std::string_view account_id, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }
    const auto id = parse_account_id(account_id);
    if (!id.has_value()) {
        return not_found();
    }

    const auto existing = find_account_record(handle.db, *id);
    if (!existing.has_value()) {
        return not_found();
    }
    if (normalize_account_status(existing->status) != "active") {
        return account_not_active();
    }

    if (!db::set_account_routing_pinned(handle.db, *id, true)) {
        return not_found();
    }

    const auto updated = find_account_record(handle.db, *id);
    if (!updated.has_value()) {
        return not_found();
    }
    return {
        .status = 200,
        .account = to_payload(*updated),
    };
}

AccountMutationResponse unpin_account(const std::string_view account_id, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }
    const auto id = parse_account_id(account_id);
    if (!id.has_value()) {
        return not_found();
    }

    if (!db::set_account_routing_pinned(handle.db, *id, false)) {
        return not_found();
    }

    const auto updated = find_account_record(handle.db, *id);
    if (!updated.has_value()) {
        return not_found();
    }
    return {
        .status = 200,
        .account = to_payload(*updated),
    };
}

AccountMutationResponse pause_account(const std::string_view account_id, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }
    const auto id = parse_account_id(account_id);
    if (!id.has_value()) {
        return not_found();
    }
    const auto updated = db::update_account_status(handle.db, *id, "paused");
    if (!updated.has_value()) {
        return not_found();
    }
    return {
        .status = 200,
        .account = to_payload(*updated),
    };
}

AccountMutationResponse reactivate_account(const std::string_view account_id, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }
    const auto id = parse_account_id(account_id);
    if (!id.has_value()) {
        return not_found();
    }
    const auto updated = db::update_account_status(handle.db, *id, "active");
    if (!updated.has_value()) {
        return not_found();
    }
    return {
        .status = 200,
        .account = to_payload(*updated),
    };
}

AccountMutationResponse delete_account(const std::string_view account_id, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }
    const auto id = parse_account_id(account_id);
    if (!id.has_value()) {
        return not_found();
    }
    if (!db::delete_account(handle.db, *id)) {
        return not_found();
    }
    return {
        .status = 200,
    };
}

AccountMutationResponse refresh_account_token(const std::string_view account_id, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }

    const auto id = parse_account_id(account_id);
    if (!id.has_value()) {
        return not_found();
    }
    const auto existing = find_account_record(handle.db, *id);
    if (!existing.has_value()) {
        return not_found();
    }

    const auto credentials = db::account_usage_credentials(handle.db, *id);
    if (!credentials.has_value()) {
        return {
            .status = 400,
            .code = "account_token_refresh_unavailable",
            .message = "Account token refresh credentials are unavailable",
        };
    }

    const auto refresh = auth::oauth::refresh_access_token_for_account(handle.db, credentials->chatgpt_account_id);
    if (!refresh.refreshed) {
        return {
            .status = 502,
            .code = "token_refresh_failed",
            .message = token_refresh_failure_message(
                refresh,
                existing->email.empty() ? std::to_string(existing->id) : existing->email
            ),
        };
    }

    const auto updated = find_account_record(handle.db, *id);
    if (!updated.has_value()) {
        return not_found();
    }
    const auto usage_cost_map = load_account_usage_cost_map(handle.db);
    return {
        .status = 200,
        .account = to_payload(*updated, &usage_cost_map),
    };
}

AccountMutationResponse refresh_account_usage(const std::string_view account_id, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }

    const auto id = parse_account_id(account_id);
    if (!id.has_value()) {
        return not_found();
    }
    const auto existing = find_account_record(handle.db, *id);
    if (!existing.has_value()) {
        return not_found();
    }

    const auto credentials = db::account_usage_credentials(handle.db, *id);
    if (!credentials.has_value()) {
        return {
            .status = 400,
            .code = "account_usage_unavailable",
            .message = "Account usage credentials are unavailable",
        };
    }

    auto active_credentials = *credentials;
    auto usage_snapshot = usage::fetch_usage_payload(active_credentials.access_token, active_credentials.chatgpt_account_id);
    std::optional<usage::UsageValidationResult> usage_validation;
    if (!usage_snapshot.has_value()) {
        usage_validation = usage::validate_usage_identity(active_credentials.access_token, active_credentials.chatgpt_account_id);
        if (usage_validation_requires_token_refresh(*usage_validation)) {
            const auto refresh =
                auth::oauth::refresh_access_token_for_account(handle.db, active_credentials.chatgpt_account_id);
            if (refresh.refreshed) {
                if (const auto refreshed_credentials = db::account_usage_credentials(handle.db, *id);
                    refreshed_credentials.has_value()) {
                    active_credentials = *refreshed_credentials;
                    usage_snapshot =
                        usage::fetch_usage_payload(active_credentials.access_token, active_credentials.chatgpt_account_id);
                    if (!usage_snapshot.has_value()) {
                        usage_validation = usage::validate_usage_identity(
                            active_credentials.access_token,
                            active_credentials.chatgpt_account_id
                        );
                    } else {
                        usage_validation.reset();
                    }
                }
            }
        }
    }
    if (!usage_snapshot.has_value()) {
        const auto validation = usage_validation.value_or(
            usage::validate_usage_identity(active_credentials.access_token, active_credentials.chatgpt_account_id)
        );
        if (usage_validation_indicates_deactivated(validation)) {
            if (!db::update_account_usage_telemetry(
                    handle.db,
                    *id,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::optional<std::string>{std::string("deactivated")}
                )) {
                return {
                    .status = 500,
                    .code = "usage_refresh_persist_failed",
                    .message = "Failed to persist usage telemetry",
                };
            }
            const auto deactivated = find_account_record(handle.db, *id);
            if (!deactivated.has_value()) {
                return not_found();
            }
            const auto usage_cost_map = load_account_usage_cost_map(handle.db);
            return {
                .status = 200,
                .account = to_payload(*deactivated, &usage_cost_map),
            };
        }

        return {
            .status = 502,
            .code = "usage_refresh_failed",
            .message = usage_refresh_failure_message(
                validation,
                existing->email.empty() ? std::to_string(existing->id) : existing->email
            ),
        };
    }

    std::optional<int> quota_primary_percent;
    std::optional<int> quota_secondary_percent;
    std::optional<int> quota_primary_window_seconds;
    std::optional<int> quota_secondary_window_seconds;
    std::optional<std::int64_t> quota_primary_reset_at_ms;
    std::optional<std::int64_t> quota_secondary_reset_at_ms;
    const auto captured_now_ms = now_ms();
    if (usage_snapshot->rate_limit.has_value()) {
        if (usage_snapshot->rate_limit->primary_window.has_value()) {
            quota_primary_percent = normalized_percent(usage_snapshot->rate_limit->primary_window->used_percent);
            quota_primary_window_seconds =
                normalized_window_seconds(usage_snapshot->rate_limit->primary_window->limit_window_seconds);
            quota_primary_reset_at_ms =
                normalized_reset_at_ms(*usage_snapshot->rate_limit->primary_window, captured_now_ms);
        }
        if (usage_snapshot->rate_limit->secondary_window.has_value()) {
            quota_secondary_percent = normalized_percent(usage_snapshot->rate_limit->secondary_window->used_percent);
            quota_secondary_window_seconds =
                normalized_window_seconds(usage_snapshot->rate_limit->secondary_window->limit_window_seconds);
            quota_secondary_reset_at_ms =
                normalized_reset_at_ms(*usage_snapshot->rate_limit->secondary_window, captured_now_ms);
        }
    }

    auto plan_type = core::text::trim_ascii(usage_snapshot->plan_type);
    const std::optional<std::string> next_plan_type = plan_type.empty()
        ? std::nullopt
        : std::optional<std::string>{std::move(plan_type)};
    std::string_view plan_for_status{};
    if (next_plan_type.has_value()) {
        plan_for_status = *next_plan_type;
    } else if (existing->plan_type.has_value()) {
        plan_for_status = *existing->plan_type;
    }
    const auto next_status =
        reconcile_account_status_from_usage(existing->status, quota_primary_percent, quota_secondary_percent, plan_for_status);
    if (!db::update_account_usage_telemetry(
            handle.db,
            *id,
            quota_primary_percent,
            quota_secondary_percent,
            next_plan_type,
            next_status,
            quota_primary_window_seconds,
            quota_secondary_window_seconds,
            quota_primary_reset_at_ms,
            quota_secondary_reset_at_ms
        )) {
        return {
            .status = 500,
            .code = "usage_refresh_persist_failed",
            .message = "Failed to persist usage telemetry",
        };
    }

    const auto refreshed = find_account_record(handle.db, *id);
    if (!refreshed.has_value()) {
        return not_found();
    }

    const auto usage_cost_map = load_account_usage_cost_map(handle.db);

    return {
        .status = 200,
        .account = to_payload(*refreshed, &usage_cost_map),
    };
}

AccountTokenMigrationResponse migrate_account_token_storage(const bool dry_run, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }

    if (!dry_run) {
        std::string encryption_error;
        if (!::tightrope::auth::crypto::token_storage_encryption_ready(&encryption_error)) {
            return {
                .status = 400,
                .code = "token_encryption_not_ready",
                .message = encryption_error.empty() ? std::string("Token encryption key is not configured") : std::move(encryption_error),
            };
        }
    }

    const auto migrated = db::migrate_plaintext_account_tokens(handle.db, dry_run);
    if (!migrated.has_value()) {
        return {
            .status = 500,
            .code = "token_migration_failed",
            .message = "Failed to migrate account token storage",
        };
    }

    return {
        .status = 200,
        .migration = to_payload(*migrated, dry_run),
    };
}

AccountTrafficResponse list_account_proxy_traffic(sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }

    AccountTrafficResponse response{
        .status = 200,
        .generated_at_ms = now_ms(),
    };
    for (const auto& snapshot : proxy::snapshot_account_traffic()) {
        if (snapshot.account_id.empty()) {
            continue;
        }
        response.accounts.push_back({
            .account_id = snapshot.account_id,
            .up_bytes = snapshot.up_bytes,
            .down_bytes = snapshot.down_bytes,
            .last_up_at_ms = snapshot.last_up_at_ms,
            .last_down_at_ms = snapshot.last_down_at_ms,
        });
    }
    return response;
}

} // namespace tightrope::server::controllers
