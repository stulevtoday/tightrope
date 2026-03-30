#include "usage_controller.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <SQLiteCpp/Column.h>
#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>
#include <glaze/glaze.hpp>

#include "controller_db.h"
#include "openai/error_envelope.h"
#include "repositories/account_repo.h"
#include "repositories/sqlite_repo_utils.h"
#include "text/ascii.h"
#include "usage_fetcher.h"

namespace tightrope::server::controllers {

namespace {

using Json = glz::generic;
using JsonArray = Json::array_t;
using JsonObject = Json::object_t;

std::string detect_plan_type(sqlite3* db) {
    if (!db::ensure_accounts_schema(db)) {
        return "guest";
    }
    auto db_handle = db::sqlite_repo_utils::resolve_database(db);
    if (!db_handle.valid()) {
        return "guest";
    }

    constexpr const char* kActiveCountSql = "SELECT COUNT(1) FROM accounts WHERE status = 'active';";
    constexpr const char* kPlanSql = R"SQL(
SELECT DISTINCT lower(trim(plan_type))
FROM accounts
WHERE status = 'active' AND plan_type IS NOT NULL AND trim(plan_type) != ''
ORDER BY 1 ASC;
)SQL";

    std::int64_t active_count = 0;
    try {
        SQLite::Statement count_stmt(*db_handle.db, kActiveCountSql);
        if (count_stmt.executeStep() && !count_stmt.getColumn(0).isNull()) {
            active_count = count_stmt.getColumn(0).getInt64();
        }
    } catch (...) {
        return "guest";
    }

    if (active_count <= 0) {
        return "guest";
    }

    std::vector<std::string> plans;
    try {
        SQLite::Statement plan_stmt(*db_handle.db, kPlanSql);
        while (plan_stmt.executeStep()) {
            if (!plan_stmt.getColumn(0).isNull()) {
                plans.push_back(core::text::trim_ascii(plan_stmt.getColumn(0).getString()));
            }
        }
    } catch (...) {
        return "guest";
    }

    plans.erase(
        std::remove_if(
            plans.begin(),
            plans.end(),
            [](const std::string& value) { return value.empty(); }
        ),
        plans.end()
    );
    if (plans.empty()) {
        return "guest";
    }
    if (plans.size() == 1) {
        return plans.front();
    }
    return "mixed";
}

std::optional<std::string> extract_bearer_token(const proxy::openai::HeaderMap& headers) {
    const auto authorization = core::text::find_value_case_insensitive(headers, "authorization");
    if (!authorization.has_value()) {
        return std::nullopt;
    }
    const auto value = core::text::trim_ascii(*authorization);
    if (value.size() < 7) {
        return std::nullopt;
    }
    if (!core::text::starts_with(core::text::to_lower_ascii(value), "bearer ")) {
        return std::nullopt;
    }
    const auto token = core::text::trim_ascii(std::string_view(value).substr(7));
    if (token.empty()) {
        return std::nullopt;
    }
    return token;
}

std::optional<std::string> extract_account_id(const proxy::openai::HeaderMap& headers) {
    const auto value = core::text::find_value_case_insensitive(headers, "chatgpt-account-id");
    if (!value.has_value()) {
        return std::nullopt;
    }
    const auto trimmed = core::text::trim_ascii(*value);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    return trimmed;
}

Json usage_window_to_json(const usage::UsageWindowSnapshot& window) {
    Json object = JsonObject{};
    if (window.used_percent.has_value()) {
        object["used_percent"] = *window.used_percent;
    }
    if (window.limit_window_seconds.has_value()) {
        object["limit_window_seconds"] = *window.limit_window_seconds;
    }
    if (window.reset_after_seconds.has_value()) {
        object["reset_after_seconds"] = *window.reset_after_seconds;
    }
    if (window.reset_at.has_value()) {
        object["reset_at"] = static_cast<double>(*window.reset_at);
    }
    return object;
}

Json usage_rate_limit_to_json(const std::optional<usage::UsageRateLimitDetails>& rate_limit) {
    if (!rate_limit.has_value()) {
        return Json{};
    }
    Json object = JsonObject{};
    object["allowed"] = rate_limit->allowed;
    object["limit_reached"] = rate_limit->limit_reached;
    if (rate_limit->primary_window.has_value()) {
        object["primary_window"] = usage_window_to_json(*rate_limit->primary_window);
    } else {
        object["primary_window"] = Json{};
    }
    if (rate_limit->secondary_window.has_value()) {
        object["secondary_window"] = usage_window_to_json(*rate_limit->secondary_window);
    } else {
        object["secondary_window"] = Json{};
    }
    return object;
}

Json usage_credits_to_json(const std::optional<usage::UsageCreditsDetails>& credits) {
    if (!credits.has_value()) {
        return Json{};
    }
    Json object = JsonObject{};
    object["has_credits"] = credits->has_credits;
    object["unlimited"] = credits->unlimited;
    if (credits->balance.has_value()) {
        object["balance"] = *credits->balance;
    } else {
        object["balance"] = Json{};
    }
    object["approx_local_messages"] = Json{};
    object["approx_cloud_messages"] = Json{};
    return object;
}

Json usage_additional_limits_to_json(const std::vector<usage::UsageAdditionalRateLimit>& additional_rate_limits) {
    Json array = JsonArray{};
    for (const auto& limit : additional_rate_limits) {
        Json object = JsonObject{};
        if (limit.quota_key.has_value()) {
            object["quota_key"] = *limit.quota_key;
        } else {
            object["quota_key"] = Json{};
        }
        object["limit_name"] = limit.limit_name;
        if (limit.display_label.has_value()) {
            object["display_label"] = *limit.display_label;
        } else {
            object["display_label"] = Json{};
        }
        object["metered_feature"] = limit.metered_feature;
        object["rate_limit"] = usage_rate_limit_to_json(limit.rate_limit);
        array.get_array().push_back(std::move(object));
    }
    return array;
}

std::string build_usage_payload(
    const std::string& fallback_plan_type,
    const std::optional<usage::UsagePayloadSnapshot>& usage_snapshot
) {
    Json payload = JsonObject{};
    if (usage_snapshot.has_value()) {
        payload["plan_type"] = usage_snapshot->plan_type.empty() ? fallback_plan_type : usage_snapshot->plan_type;
        payload["rate_limit"] = usage_rate_limit_to_json(usage_snapshot->rate_limit);
        payload["credits"] = usage_credits_to_json(usage_snapshot->credits);
        payload["additional_rate_limits"] = usage_additional_limits_to_json(usage_snapshot->additional_rate_limits);
    } else {
        payload["plan_type"] = fallback_plan_type;
        payload["rate_limit"] = Json{};
        payload["credits"] = Json{};
        payload["additional_rate_limits"] = JsonArray{};
    }
    const auto serialized = glz::write_json(payload);
    if (!serialized) {
        return std::string(R"({"plan_type":"guest","rate_limit":null,"credits":null,"additional_rate_limits":[]})");
    }
    return serialized.value_or(std::string("{}"));
}

} // namespace

CodexUsageResponse get_codex_usage(const proxy::openai::HeaderMap& headers, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .body = proxy::openai::build_error_envelope("db_unavailable", "Database unavailable", "server_error"),
        };
    }

    std::optional<usage::UsagePayloadSnapshot> usage_snapshot;
    const auto token = extract_bearer_token(headers);
    const auto account_id = extract_account_id(headers);
    if (token.has_value() && account_id.has_value()) {
        usage_snapshot = usage::fetch_usage_payload(*token, *account_id);
    }

    return {
        .status = 200,
        .body = build_usage_payload(detect_plan_type(handle.db), usage_snapshot),
    };
}

} // namespace tightrope::server::controllers
