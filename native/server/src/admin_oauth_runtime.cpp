#include "internal/admin_runtime_parts.h"

#include <optional>
#include <string>

#include "internal/admin_runtime_common.h"
#include "internal/http_runtime_utils.h"
#include "text/json_escape.h"
#include "controllers/oauth_controller.h"

namespace tightrope::server::internal::admin {

namespace {

std::string nullable_json(const std::optional<std::string>& value) {
    return value.has_value() ? core::text::quote_json_string(*value) : "null";
}

std::string nullable_json(const std::optional<int>& value) {
    return value.has_value() ? std::to_string(*value) : "null";
}

std::string oauth_start_json(const controllers::OauthStartResponse& response) {
    return std::string(R"({"method":)") + core::text::quote_json_string(response.method) + R"(,"authorizationUrl":)" +
           nullable_json(response.authorization_url) + R"(,"callbackUrl":)" + nullable_json(response.callback_url) +
           R"(,"verificationUrl":)" + nullable_json(response.verification_url) + R"(,"userCode":)" +
           nullable_json(response.user_code) + R"(,"deviceAuthId":)" + nullable_json(response.device_auth_id) +
           R"(,"intervalSeconds":)" + nullable_json(response.interval_seconds) + R"(,"expiresInSeconds":)" +
           nullable_json(response.expires_in_seconds) + "}";
}

std::string oauth_status_json(const controllers::OauthStatusResponse& response) {
    return std::string(R"({"status":)") + core::text::quote_json_string(response.oauth_status) + R"(,"errorMessage":)" +
           nullable_json(response.error_message) + R"(,"listenerRunning":)" +
           (response.listener_running ? "true" : "false") + R"(,"callbackUrl":)" +
           nullable_json(response.callback_url) + R"(,"authorizationUrl":)" + nullable_json(response.authorization_url) +
           "}";
}

std::string oauth_complete_json(const controllers::OauthCompleteResponse& response) {
    return std::string(R"({"status":)") + core::text::quote_json_string(response.oauth_status) + "}";
}

std::string oauth_manual_callback_json(const controllers::ManualCallbackResponse& response) {
    return std::string(R"({"status":)") + core::text::quote_json_string(response.oauth_status) + R"(,"errorMessage":)" +
           nullable_json(response.error_message) + "}";
}

} // namespace

void wire_oauth_routes(uWS::App& app) {
    app.post("/api/oauth/start", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        http::read_request_body(res, [res](std::string body) mutable {
            controllers::OauthStartRequest request;
            if (!body.empty()) {
                const auto parsed = parse_json_object(body);
                if (!parsed.has_value()) {
                    http::write_json(res, 400, dashboard_error_json("invalid_request", "Invalid JSON payload"));
                    return;
                }
                request.force_method = json_string(*parsed, "forceMethod")
                                           .or_else([&] { return json_string(*parsed, "force_method"); });
            }

            const auto response = controllers::start_oauth(request);
            if (response.status == 200) {
                http::write_json(res, 200, oauth_start_json(response));
                return;
            }
            http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
        });
    });

    app.get("/api/oauth/status", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        const auto response = controllers::oauth_status();
        if (response.status == 200) {
            http::write_json(res, 200, oauth_status_json(response));
            return;
        }
        http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
    });

    app.post("/api/oauth/stop", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        const auto response = controllers::stop_oauth();
        if (response.status == 200) {
            http::write_json(res, 200, oauth_status_json(response));
            return;
        }
        http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
    });

    app.post("/api/oauth/restart", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        const auto response = controllers::restart_oauth();
        if (response.status == 200) {
            http::write_json(res, 200, oauth_start_json(response));
            return;
        }
        http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
    });

    app.post("/api/oauth/complete", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        http::read_request_body(res, [res](std::string body) mutable {
            controllers::OauthCompleteRequest request;
            if (!body.empty()) {
                const auto parsed = parse_json_object(body);
                if (!parsed.has_value()) {
                    http::write_json(res, 400, dashboard_error_json("invalid_request", "Invalid JSON payload"));
                    return;
                }
                request.device_auth_id = json_string(*parsed, "deviceAuthId")
                                             .or_else([&] { return json_string(*parsed, "device_auth_id"); });
                request.user_code = json_string(*parsed, "userCode")
                                        .or_else([&] { return json_string(*parsed, "user_code"); });
            }

            const auto response = controllers::complete_oauth(request);
            if (response.status == 200) {
                http::write_json(res, 200, oauth_complete_json(response));
                return;
            }
            http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
        });
    });

    app.post("/api/oauth/manual-callback", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        http::read_request_body(res, [res](std::string body) mutable {
            const auto parsed = parse_json_object(body);
            if (!parsed.has_value()) {
                http::write_json(res, 400, dashboard_error_json("invalid_request", "Invalid JSON payload"));
                return;
            }

            const auto callback_url = json_string(*parsed, "callbackUrl")
                                          .or_else([&] { return json_string(*parsed, "callback_url"); });
            if (!callback_url.has_value() || callback_url->empty()) {
                http::write_json(res, 400, dashboard_error_json("invalid_callback", "callbackUrl is required"));
                return;
            }

            const auto response = controllers::manual_oauth_callback(*callback_url);
            if (response.status == 200) {
                http::write_json(res, 200, oauth_manual_callback_json(response));
                return;
            }
            http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
        });
    });

    app.get("/auth/callback", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        const auto code = http::to_string_safe(req->getQuery("code"));
        const auto state = http::to_string_safe(req->getQuery("state"));
        const auto error = http::to_string_safe(req->getQuery("error"));
        const auto response = controllers::browser_oauth_callback(code, state, error);
        http::write_http(res, response.status, response.content_type, response.body);
    });
}

} // namespace tightrope::server::internal::admin
