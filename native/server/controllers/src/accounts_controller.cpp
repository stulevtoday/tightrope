#include "accounts_controller.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <optional>

#include "controller_db.h"
#include "account_traffic.h"
#include "repositories/account_repo.h"
#include "text/ascii.h"
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

AccountPayload to_payload(const db::AccountRecord& record) {
    return {
        .account_id = std::to_string(record.id),
        .email = record.email,
        .provider = record.provider,
        .status = record.status,
        .plan_type = record.plan_type,
        .quota_primary_percent = record.quota_primary_percent,
        .quota_secondary_percent = record.quota_secondary_percent,
    };
}

AccountMutationResponse not_found() {
    return {
        .status = 404,
        .code = "account_not_found",
        .message = "Account not found",
    };
}

std::optional<int> normalized_percent(const std::optional<int> value) {
    if (!value.has_value()) {
        return std::nullopt;
    }
    return std::clamp(*value, 0, 100);
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

std::int64_t now_ms() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
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

    AccountsResponse response{.status = 200};
    for (const auto& record : db::list_accounts(handle.db)) {
        response.accounts.push_back(to_payload(record));
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

    const auto credentials = db::account_usage_credentials(handle.db, *id);
    if (!credentials.has_value()) {
        return {
            .status = 400,
            .code = "account_usage_unavailable",
            .message = "Account usage credentials are unavailable",
        };
    }

    const auto usage_snapshot = usage::fetch_usage_payload(credentials->access_token, credentials->chatgpt_account_id);
    if (!usage_snapshot.has_value()) {
        return {
            .status = 502,
            .code = "usage_refresh_failed",
            .message = "Failed to fetch usage telemetry from provider",
        };
    }

    std::optional<int> quota_primary_percent;
    std::optional<int> quota_secondary_percent;
    if (usage_snapshot->rate_limit.has_value()) {
        if (usage_snapshot->rate_limit->primary_window.has_value()) {
            quota_primary_percent = normalized_percent(usage_snapshot->rate_limit->primary_window->used_percent);
        }
        if (usage_snapshot->rate_limit->secondary_window.has_value()) {
            quota_secondary_percent = normalized_percent(usage_snapshot->rate_limit->secondary_window->used_percent);
        }
    }

    auto plan_type = core::text::trim_ascii(usage_snapshot->plan_type);
    const std::optional<std::string> next_plan_type = plan_type.empty()
        ? std::nullopt
        : std::optional<std::string>{std::move(plan_type)};
    if (!db::update_account_usage_telemetry(
            handle.db,
            *id,
            quota_primary_percent,
            quota_secondary_percent,
            next_plan_type
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

    return {
        .status = 200,
        .account = to_payload(*refreshed),
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
