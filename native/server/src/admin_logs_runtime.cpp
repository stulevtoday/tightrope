#include "internal/admin_runtime_parts.h"

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "internal/admin_runtime_common.h"
#include "internal/http_runtime_utils.h"
#include "text/json_escape.h"
#include "controllers/logs_controller.h"

namespace tightrope::server::internal::admin {

namespace {

constexpr std::size_t kDefaultLogsLimit = 200;

std::optional<std::size_t> parse_size_query(const std::string_view value) {
    if (value.data() == nullptr || value.empty()) {
        return std::nullopt;
    }
    std::size_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

std::string request_log_json(const controllers::RequestLogPayload& log) {
    return std::string(R"({"id":)") + std::to_string(log.id) + R"(,"accountId":)" +
           (log.account_id.has_value() ? core::text::quote_json_string(std::to_string(*log.account_id)) : "null") +
           R"(,"path":)" + core::text::quote_json_string(log.path) + R"(,"method":)" +
           core::text::quote_json_string(log.method) + R"(,"statusCode":)" + std::to_string(log.status_code) +
           R"(,"model":)" + optional_string_json(log.model) + R"(,"requestedAt":)" +
           core::text::quote_json_string(log.requested_at) + R"(,"errorCode":)" +
           optional_string_json(log.error_code) + R"(,"transport":)" + optional_string_json(log.transport) +
           R"(,"totalCost":)" + number_json(log.total_cost) + "}";
}

std::string request_logs_json(const controllers::RequestLogsResponse& response) {
    std::string body = std::string(R"({"limit":)") + std::to_string(response.limit) + R"(,"offset":)" +
                       std::to_string(response.offset) + R"(,"logs":[)";
    for (std::size_t i = 0; i < response.logs.size(); ++i) {
        if (i > 0) {
            body.push_back(',');
        }
        body += request_log_json(response.logs[i]);
    }
    body += "]}";
    return body;
}

void list_logs_route(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    const auto limit = parse_size_query(req->getQuery("limit")).value_or(kDefaultLogsLimit);
    const auto offset = parse_size_query(req->getQuery("offset")).value_or(0);

    const auto response = controllers::list_request_logs(limit, offset);
    if (response.status == 200) {
        http::write_json(res, 200, request_logs_json(response));
        return;
    }
    http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
}

} // namespace

void wire_logs_routes(uWS::App& app) {
    app.get("/api/logs", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) { list_logs_route(res, req); });
    app.get("/api/logs/", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) { list_logs_route(res, req); });
}

} // namespace tightrope::server::internal::admin
