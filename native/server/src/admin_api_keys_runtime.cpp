#include "internal/admin_runtime_parts.h"

#include <string>

#include "internal/admin_runtime_common.h"
#include "internal/http_runtime_utils.h"
#include "text/json_escape.h"
#include "controllers/keys_controller.h"

namespace tightrope::server::internal::admin {

namespace {

std::string api_key_limit_json(const controllers::ApiKeyLimitPayload& limit) {
    return std::string(R"({"id":)") + std::to_string(limit.id) + R"(,"limitType":)" +
           core::text::quote_json_string(limit.limit_type) + R"(,"limitWindow":)" +
           core::text::quote_json_string(limit.limit_window) + R"(,"maxValue":)" + number_json(limit.max_value) +
           R"(,"currentValue":)" + number_json(limit.current_value) + R"(,"modelFilter":)" +
           optional_string_json(limit.model_filter) + R"(,"resetAt":null})";
}

std::string api_key_json(const controllers::ApiKeyPayload& key) {
    std::string body = std::string(R"({"id":)") + core::text::quote_json_string(key.key_id) + R"(,"name":)" +
                       core::text::quote_json_string(key.name) + R"(,"keyPrefix":)" +
                       core::text::quote_json_string(key.key_prefix) + R"(,"allowedModels":[)";
    for (std::size_t i = 0; i < key.allowed_models.size(); ++i) {
        if (i > 0) {
            body.push_back(',');
        }
        body += core::text::quote_json_string(key.allowed_models[i]);
    }
    body += R"(],"enforcedModel":)" + optional_string_json(key.enforced_model) + R"(,"enforcedReasoningEffort":)" +
            optional_string_json(key.enforced_reasoning_effort) + R"(,"expiresAt":)" +
            optional_string_json(key.expires_at) + R"(,"isActive":)" + bool_json(key.is_active) +
            R"(,"createdAt":)" + core::text::quote_json_string(key.created_at) + R"(,"lastUsedAt":)" +
            optional_string_json(key.last_used_at) + R"(,"limits":[)";
    for (std::size_t i = 0; i < key.limits.size(); ++i) {
        if (i > 0) {
            body.push_back(',');
        }
        body += api_key_limit_json(key.limits[i]);
    }
    body += R"(],"usageSummary":null})";
    return body;
}

std::string api_key_create_json(const controllers::ApiKeyMutationResponse& response) {
    auto body = api_key_json(response.api_key);
    body.pop_back();
    body += R"(,"key":)" + core::text::quote_json_string(response.key) + "}";
    return body;
}

void create_api_key_route(uWS::HttpResponse<false>* res, std::string body) {
    const auto parsed = parse_json_object(body);
    if (!parsed.has_value()) {
        http::write_json(res, 400, dashboard_error_json("invalid_api_key_payload", "Invalid JSON payload"));
        return;
    }

    controllers::ApiKeyCreateRequest request;
    request.name = json_string(*parsed, "name").value_or("");
    const auto allowed_models = json_string_array(*parsed, "allowedModels");
    if (!allowed_models.empty()) {
        request.allowed_models = allowed_models;
    }
    request.enforced_model = json_string(*parsed, "enforcedModel");
    request.enforced_reasoning_effort = json_string(*parsed, "enforcedReasoningEffort");
    request.expires_at = json_string(*parsed, "expiresAt");

    const auto response = controllers::create_api_key(request);
    if (response.status == 201) {
        http::write_json(res, 201, api_key_create_json(response));
        return;
    }
    http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
}

void list_api_keys_route(uWS::HttpResponse<false>* res) {
    const auto response = controllers::list_api_keys();
    if (response.status == 200) {
        std::string body = "[";
        for (std::size_t i = 0; i < response.items.size(); ++i) {
            if (i > 0) {
                body.push_back(',');
            }
            body += api_key_json(response.items[i]);
        }
        body += "]";
        http::write_json(res, 200, body);
        return;
    }
    http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
}

} // namespace

void wire_api_keys_routes(uWS::App& app) {
    app.post("/api/api-keys", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        http::read_request_body(res, [res](std::string body) mutable { create_api_key_route(res, std::move(body)); });
    });

    app.post("/api/api-keys/", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        http::read_request_body(res, [res](std::string body) mutable { create_api_key_route(res, std::move(body)); });
    });

    app.get("/api/api-keys", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        list_api_keys_route(res);
    });

    app.get("/api/api-keys/", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        list_api_keys_route(res);
    });

    app.patch("/api/api-keys/:key_id", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        const auto key_id = http::decode_percent_escapes(http::to_string_safe(req->getParameter(0)));
        http::read_request_body(res, [res, key_id = std::move(key_id)](std::string body) mutable {
            const auto parsed = parse_json_object(body);
            if (!parsed.has_value()) {
                http::write_json(res, 400, dashboard_error_json("invalid_api_key_payload", "Invalid JSON payload"));
                return;
            }

            controllers::ApiKeyUpdateRequest request;
            request.name = json_string(*parsed, "name");
            const auto allowed_models = json_string_array(*parsed, "allowedModels");
            if (!allowed_models.empty()) {
                request.allowed_models = allowed_models;
            }
            request.enforced_model = json_string(*parsed, "enforcedModel");
            request.enforced_reasoning_effort = json_string(*parsed, "enforcedReasoningEffort");
            request.is_active = json_bool(*parsed, "isActive");
            request.expires_at = json_string(*parsed, "expiresAt");

            const auto response = controllers::update_api_key(key_id, request);
            if (response.status == 200) {
                http::write_json(res, 200, api_key_json(response.api_key));
                return;
            }
            http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
        });
    });

    app.post("/api/api-keys/:key_id/regenerate", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        const auto key_id = http::decode_percent_escapes(http::to_string_safe(req->getParameter(0)));
        const auto response = controllers::regenerate_api_key(key_id);
        if (response.status == 200) {
            http::write_json(res, 200, api_key_create_json(response));
            return;
        }
        http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
    });

    app.del("/api/api-keys/:key_id", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        const auto key_id = http::decode_percent_escapes(http::to_string_safe(req->getParameter(0)));
        const auto response = controllers::delete_api_key(key_id);
        if (response.status == 204) {
            http::write_http(res, 204, "", "");
            return;
        }
        http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
    });
}

} // namespace tightrope::server::internal::admin
