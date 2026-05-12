#include "internal/admin_runtime_parts.h"

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "controllers/sessions_controller.h"
#include "internal/admin_runtime_common.h"
#include "internal/http_runtime_utils.h"
#include "text/json_escape.h"

namespace tightrope::server::internal::admin {

namespace {

constexpr std::size_t kDefaultSessionsLimit = 500;

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

std::string sticky_session_json(const controllers::StickySessionPayload& session) {
    return std::string(R"({"sessionKey":)") + core::text::quote_json_string(session.session_key) + R"(,"accountId":)" +
           core::text::quote_json_string(session.account_id) + R"(,"kind":)" +
           core::text::quote_json_string(session.kind) + R"(,"updatedAtMs":)" +
           std::to_string(session.updated_at_ms) + R"(,"expiresAtMs":)" + std::to_string(session.expires_at_ms) + "}";
}

std::string sticky_sessions_json(const controllers::StickySessionsResponse& response) {
    std::string body = std::string(R"({"generatedAtMs":)") + std::to_string(response.generated_at_ms) + R"(,"sessions":[)";
    for (std::size_t i = 0; i < response.sessions.size(); ++i) {
        if (i > 0) {
            body.push_back(',');
        }
        body += sticky_session_json(response.sessions[i]);
    }
    body += "]}";
    return body;
}

std::string sticky_sessions_purge_json(const controllers::StickySessionsPurgeResponse& response) {
    return std::string(R"({"generatedAtMs":)") + std::to_string(response.generated_at_ms) + R"(,"purged":)" +
           std::to_string(response.purged) + "}";
}

void list_sessions_route(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    const auto limit = parse_size_query(req->getQuery("limit")).value_or(kDefaultSessionsLimit);
    const auto offset = parse_size_query(req->getQuery("offset")).value_or(0);
    const auto response = controllers::list_sticky_sessions(limit, offset);
    if (response.status == 200) {
        http::write_json(res, 200, sticky_sessions_json(response));
        return;
    }
    http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
}

void purge_sessions_route(uWS::HttpResponse<false>* res) {
    const auto response = controllers::purge_stale_sticky_sessions();
    if (response.status == 200) {
        http::write_json(res, 200, sticky_sessions_purge_json(response));
        return;
    }
    http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
}

} // namespace

void wire_sessions_routes(uWS::App& app) {
    app.get("/api/sessions", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) { list_sessions_route(res, req); });
    app.get("/api/sessions/", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) { list_sessions_route(res, req); });
    app.post("/api/sessions/purge-stale", [](uWS::HttpResponse<false>* res, uWS::HttpRequest*) { purge_sessions_route(res); });
    app.post("/api/sessions/purge-stale/", [](uWS::HttpResponse<false>* res, uWS::HttpRequest*) { purge_sessions_route(res); });
}

} // namespace tightrope::server::internal::admin
