#include "internal/proxy_request_logger.h"

#include <optional>
#include <string>
#include <string_view>

#include <glaze/glaze.hpp>

#include "controllers/controller_db.h"
#include "internal/proxy_error_policy.h"
#include "logging/logger.h"
#include "repositories/request_log_repo.h"
#include "text/ascii.h"

namespace tightrope::server::internal {

namespace {

using Json = glz::generic;
using JsonObject = Json::object_t;

std::optional<std::string> header_value_case_insensitive(
    const proxy::openai::HeaderMap* headers,
    const std::string_view key
) {
    if (headers == nullptr) {
        return std::nullopt;
    }
    for (const auto& [candidate, value] : *headers) {
        if (core::text::equals_case_insensitive(candidate, key)) {
            return value;
        }
    }
    return std::nullopt;
}

std::optional<std::string> json_string(const JsonObject& object, const std::string_view key) {
    const auto it = object.find(std::string(key));
    if (it == object.end() || !it->second.is_string()) {
        return std::nullopt;
    }
    return it->second.get_string();
}

std::optional<std::string> extract_error_code_from_error_object(const JsonObject& object) {
    if (const auto code = json_string(object, "code"); code.has_value() && !code->empty()) {
        return core::text::to_lower_ascii(*code);
    }
    if (const auto type = json_string(object, "type"); type.has_value() && !type->empty()) {
        return core::text::to_lower_ascii(*type);
    }
    return std::nullopt;
}

std::optional<std::string> extract_error_code_from_json(const std::string_view body) {
    if (body.empty()) {
        return std::nullopt;
    }
    Json payload;
    if (const auto ec = glz::read_json(payload, body); ec || !payload.is_object()) {
        return std::nullopt;
    }
    const auto& object = payload.get_object();

    const auto error_it = object.find("error");
    if (error_it != object.end() && error_it->second.is_object()) {
        return extract_error_code_from_error_object(error_it->second.get_object());
    }

    const auto response_it = object.find("response");
    if (response_it != object.end() && response_it->second.is_object()) {
        const auto& response = response_it->second.get_object();
        const auto response_error_it = response.find("error");
        if (response_error_it != response.end() && response_error_it->second.is_object()) {
            return extract_error_code_from_error_object(response_error_it->second.get_object());
        }
    }

    const auto type = json_string(object, "type");
    if (!type.has_value()) {
        return std::nullopt;
    }
    if (*type == "error") {
        return std::string("upstream_error");
    }
    if (*type == "response.failed") {
        return std::string("upstream_error");
    }
    return std::nullopt;
}

std::optional<std::string> extract_model_from_request_body(const std::string_view body) {
    if (body.empty()) {
        return std::nullopt;
    }
    Json payload;
    if (const auto ec = glz::read_json(payload, body); ec || !payload.is_object()) {
        return std::nullopt;
    }
    return json_string(payload.get_object(), "model");
}

std::optional<std::int64_t> resolve_account_id(sqlite3* db, const proxy::openai::HeaderMap* headers) {
    if (db == nullptr) {
        return std::nullopt;
    }
    const auto raw_account = header_value_case_insensitive(headers, "chatgpt-account-id");
    if (!raw_account.has_value()) {
        return std::nullopt;
    }
    const auto account = core::text::trim_ascii(*raw_account);
    if (account.empty()) {
        return std::nullopt;
    }
    return db::find_account_id_by_chatgpt_account_id(db, account);
}

std::optional<std::string> resolve_error_code(const ProxyRequestLogContext& context) {
    if (context.status_code < 400) {
        return std::nullopt;
    }

    if (!context.response_events.empty()) {
        const auto code = proxy::internal::extract_error_code_from_stream_events(context.response_events);
        if (!code.empty()) {
            return code;
        }
    }

    if (context.response_body.has_value()) {
        const auto code = extract_error_code_from_json(*context.response_body);
        if (code.has_value()) {
            return code;
        }
    }
    return std::string("upstream_error");
}

} // namespace

void persist_proxy_request_log(const ProxyRequestLogContext& context) noexcept {
    if (context.method.empty() || context.route.empty()) {
        return;
    }

    auto db_handle = controllers::open_controller_db();
    if (db_handle.db == nullptr || !db::ensure_request_log_schema(db_handle.db)) {
        return;
    }

    db::RequestLogWrite write;
    write.account_id = resolve_account_id(db_handle.db, context.headers);
    write.path = std::string(context.route);
    write.method = std::string(context.method);
    write.status_code = context.status_code;
    write.model = extract_model_from_request_body(context.request_body);
    write.error_code = resolve_error_code(context);
    if (!context.transport.empty()) {
        write.transport = std::string(context.transport);
    }
    write.total_cost = 0.0;

    if (!db::append_request_log(db_handle.db, write)) {
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy_server",
            "request_log_persist_failed",
            "route=" + std::string(context.route) + " status=" + std::to_string(context.status_code)
        );
    }
}

} // namespace tightrope::server::internal
