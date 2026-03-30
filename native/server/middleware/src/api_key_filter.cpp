#include "api_key_filter.h"

#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <algorithm>

#include <glaze/glaze.hpp>

#include "api_keys/key_validator.h"
#include "api_keys/limit_enforcer.h"
#include "openai/error_envelope.h"
#include "repositories/account_repo.h"
#include "repositories/api_key_repo.h"
#include "repositories/settings_repo.h"
#include "text/ascii.h"
#include "usage_fetcher.h"

namespace tightrope::server::middleware {

namespace {

using Json = glz::generic;
using JsonObject = Json::object_t;

std::optional<std::string> find_header_case_insensitive(
    const proxy::openai::HeaderMap& headers,
    const std::string_view key
) {
    for (const auto& [candidate, value] : headers) {
        if (core::text::equals_case_insensitive(candidate, key)) {
            return value;
        }
    }
    return std::nullopt;
}

std::optional<std::string> extract_bearer_token(const std::optional<std::string>& authorization) {
    if (!authorization.has_value()) {
        return std::nullopt;
    }
    const auto value = core::text::trim_ascii(std::string_view(*authorization));
    if (value.size() < 7) {
        return std::nullopt;
    }
    if (!core::text::starts_with(core::text::to_lower_ascii(value), "bearer ")) {
        return std::nullopt;
    }
    const auto token = core::text::trim_ascii(value.substr(7));
    if (token.empty()) {
        return std::nullopt;
    }
    return std::string(token);
}

std::optional<std::time_t> parse_utc_timestamp(std::string value) {
    if (value.empty()) {
        return std::nullopt;
    }
    value = std::string(core::text::trim_ascii(std::string_view(value)));
    if (!value.empty() && value.back() == 'Z') {
        value.pop_back();
    }

    std::tm parsed{};
    {
        std::istringstream stream(value);
        stream >> std::get_time(&parsed, "%Y-%m-%d %H:%M:%S");
        if (!stream.fail()) {
#if defined(_WIN32)
            return _mkgmtime(&parsed);
#else
            return timegm(&parsed);
#endif
        }
    }

    parsed = {};
    {
        std::istringstream stream(value);
        stream >> std::get_time(&parsed, "%Y-%m-%dT%H:%M:%S");
        if (!stream.fail()) {
#if defined(_WIN32)
            return _mkgmtime(&parsed);
#else
            return timegm(&parsed);
#endif
        }
    }
    return std::nullopt;
}

bool is_api_key_expired(const std::optional<std::string>& expires_at) {
    if (!expires_at.has_value() || expires_at->empty()) {
        return false;
    }
    const auto expiry = parse_utc_timestamp(*expires_at);
    if (!expiry.has_value()) {
        return false;
    }
    return *expiry <= std::time(nullptr);
}

AuthDecision deny_auth(int status, std::string_view message);
AuthDecision deny_server(std::string_view code, std::string_view message);

AuthDecision validate_proxy_api_key_impl(
    sqlite3* db,
    const proxy::openai::HeaderMap& headers,
    std::optional<db::ApiKeyRecord>* api_key_record = nullptr
) {
    if (db == nullptr) {
        return deny_server("db_unavailable", "Database unavailable");
    }

    const auto settings = db::get_dashboard_settings(db);
    if (!settings.has_value()) {
        return deny_server("settings_unavailable", "Failed to load settings");
    }
    if (!settings->api_key_auth_enabled) {
        return {};
    }

    const auto token = extract_bearer_token(find_header_case_insensitive(headers, "authorization"));
    if (!token.has_value()) {
        return deny_auth(401, "Missing API key in Authorization header");
    }

    const auto key_hash = auth::api_keys::hash_key(*token);
    if (!key_hash.has_value()) {
        return deny_auth(401, "Invalid API key");
    }

    const auto key_record = db::get_api_key_by_hash(db, *key_hash);
    if (!key_record.has_value() || !key_record->is_active) {
        return deny_auth(401, "Invalid API key");
    }
    if (is_api_key_expired(key_record->expires_at)) {
        return deny_auth(401, "API key has expired");
    }

    if (api_key_record != nullptr) {
        *api_key_record = key_record;
    }
    return {};
}

bool exists_active_chatgpt_account(sqlite3* db, const std::string_view account_id) {
    if (!db::ensure_accounts_schema(db) || account_id.empty()) {
        return false;
    }
    constexpr const char* kSql = "SELECT 1 FROM accounts WHERE chatgpt_account_id = ?1 AND status = 'active' LIMIT 1;";
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
    if (sqlite3_bind_text(stmt, 1, std::string(account_id).c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        finalize();
        return false;
    }
    const int rc = sqlite3_step(stmt);
    finalize();
    return rc == SQLITE_ROW;
}

AuthDecision deny_auth(const int status, const std::string_view message) {
    return {
        .allow = false,
        .status = status,
        .body = proxy::openai::build_error_envelope("invalid_api_key", std::string(message), "authentication_error"),
    };
}

AuthDecision deny_server(const std::string_view code, const std::string_view message) {
    return {
        .allow = false,
        .status = 500,
        .body = proxy::openai::build_error_envelope(std::string(code), std::string(message), "server_error"),
    };
}

bool policy_has_constraints(const ApiKeyModelPolicy& policy) {
    return !policy.allowed_models.empty() || (policy.enforced_model.has_value() && !policy.enforced_model->empty()) ||
           (policy.enforced_reasoning_effort.has_value() && !policy.enforced_reasoning_effort->empty());
}

std::optional<std::string> json_string(const JsonObject& object, const std::string_view key) {
    const auto it = object.find(std::string(key));
    if (it == object.end() || !it->second.is_string()) {
        return std::nullopt;
    }
    return it->second.get_string();
}

} // namespace

ApiKeyRequestPolicyDecision enforce_api_key_request_policy(
    const ApiKeyModelPolicy& policy,
    std::string& request_body
) noexcept {
    ApiKeyRequestPolicyDecision decision{};
    if (!policy_has_constraints(policy) || request_body.empty()) {
        return decision;
    }

    Json payload;
    if (const auto ec = glz::read_json(payload, request_body); ec || !payload.is_object()) {
        return decision;
    }

    auto& object = payload.get_object();
    const auto requested_model = json_string(object, "model");
    auto effective_model = requested_model;
    if (policy.enforced_model.has_value() && !policy.enforced_model->empty()) {
        effective_model = policy.enforced_model;
    }

    if (!policy.allowed_models.empty() && effective_model.has_value() && !effective_model->empty()) {
        if (std::find(policy.allowed_models.begin(), policy.allowed_models.end(), *effective_model) ==
            policy.allowed_models.end()) {
            const auto message = "This API key does not have access to model '" + *effective_model + "'";
            decision.allow = false;
            decision.status = 403;
            decision.error_code = "model_not_allowed";
            decision.error_message = message;
            decision.error_type = "permission_error";
            decision.error_body = proxy::openai::build_error_envelope(
                decision.error_code,
                decision.error_message,
                decision.error_type
            );
            return decision;
        }
    }

    if (effective_model.has_value() && !effective_model->empty()) {
        object["model"] = *effective_model;
    }
    if (policy.enforced_reasoning_effort.has_value() && !policy.enforced_reasoning_effort->empty()) {
        Json reasoning = JsonObject{};
        const auto reasoning_it = object.find("reasoning");
        if (reasoning_it != object.end() && reasoning_it->second.is_object()) {
            reasoning = reasoning_it->second;
        }
        reasoning["effort"] = *policy.enforced_reasoning_effort;
        object["reasoning"] = std::move(reasoning);
    }

    const auto serialized = glz::write_json(payload);
    if (serialized) {
        request_body = serialized.value_or(request_body);
    }
    return decision;
}

bool is_proxy_api_key_protected_path(const std::string_view path) noexcept {
    if (path == "/v1" || core::text::starts_with(path, "/v1/")) {
        return true;
    }
    if (path == "/backend-api/codex" || core::text::starts_with(path, "/backend-api/codex/")) {
        return true;
    }
    return path == "/backend-api/transcribe";
}

bool is_codex_usage_identity_path(const std::string_view path) noexcept {
    return path == "/api/codex/usage" || path == "/api/codex/usage/";
}

AuthDecision validate_proxy_api_key_request(
    sqlite3* db,
    const std::string_view path,
    const proxy::openai::HeaderMap& headers
) noexcept {
    if (!is_proxy_api_key_protected_path(path)) {
        return {};
    }
    return validate_proxy_api_key_impl(db, headers);
}

AuthDecision validate_codex_usage_identity_request(
    sqlite3* db,
    const std::string_view path,
    const proxy::openai::HeaderMap& headers
) noexcept {
    if (!is_codex_usage_identity_path(path)) {
        return {};
    }
    if (db == nullptr) {
        return deny_server("db_unavailable", "Database unavailable");
    }

    const auto token = extract_bearer_token(find_header_case_insensitive(headers, "authorization"));
    if (!token.has_value()) {
        return deny_auth(401, "Missing ChatGPT token in Authorization header");
    }

    const auto raw_account_id = find_header_case_insensitive(headers, "chatgpt-account-id");
    const auto account_id = raw_account_id.has_value() ? std::string(core::text::trim_ascii(*raw_account_id)) : std::string{};
    if (account_id.empty()) {
        return deny_auth(401, "Missing chatgpt-account-id header");
    }

    if (!exists_active_chatgpt_account(db, account_id)) {
        return deny_auth(401, "Unknown or inactive chatgpt-account-id");
    }

    const auto validation = usage::validate_usage_identity(*token, account_id);
    if (!validation.success) {
        if (validation.status_code == 429) {
            const auto message = validation.message.empty() ? "Rate limit exceeded" : validation.message;
            return {
                .allow = false,
                .status = 429,
                .body = proxy::openai::build_error_envelope(
                    "rate_limit_exceeded",
                    message,
                    "rate_limit_error"
                ),
            };
        }
        if (validation.status_code == 401 || validation.status_code == 403) {
            return deny_auth(401, "Invalid ChatGPT token or chatgpt-account-id");
        }
        return {
            .allow = false,
            .status = 502,
            .body = proxy::openai::build_error_envelope(
                "upstream_unavailable",
                "Unable to validate ChatGPT credentials at this time",
                "server_error"
            ),
        };
    }

    return {};
}

ApiKeyModelPolicy resolve_api_key_model_policy(
    sqlite3* db,
    const proxy::openai::HeaderMap& headers
) noexcept {
    ApiKeyModelPolicy policy;
    std::optional<db::ApiKeyRecord> key_record;
    const auto decision = validate_proxy_api_key_impl(db, headers, &key_record);
    if (!decision.allow || !key_record.has_value()) {
        return policy;
    }

    policy.key_id = key_record->key_id;
    policy.allowed_models = auth::api_keys::deserialize_allowed_models(key_record->allowed_models);
    if (key_record->enforced_model.has_value() && !key_record->enforced_model->empty()) {
        policy.enforced_model = *key_record->enforced_model;
    }
    if (key_record->enforced_reasoning_effort.has_value() && !key_record->enforced_reasoning_effort->empty()) {
        policy.enforced_reasoning_effort = *key_record->enforced_reasoning_effort;
    }
    return policy;
}

} // namespace tightrope::server::middleware
