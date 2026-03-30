#include "internal/admin_runtime_parts.h"

#include <optional>
#include <string>

#include "internal/admin_runtime_common.h"
#include "internal/http_runtime_utils.h"
#include "text/json_escape.h"
#include "controllers/accounts_controller.h"

namespace tightrope::server::internal::admin {

namespace {

std::string optional_string_json(const std::optional<std::string>& value) {
    if (!value.has_value()) {
        return "null";
    }
    return core::text::quote_json_string(*value);
}

std::string optional_int_json(const std::optional<int>& value) {
    if (!value.has_value()) {
        return "null";
    }
    return std::to_string(*value);
}

std::string account_json(const controllers::AccountPayload& account) {
    return std::string(R"({"accountId":)") + core::text::quote_json_string(account.account_id) + R"(,"email":)" +
           core::text::quote_json_string(account.email) + R"(,"provider":)" +
           core::text::quote_json_string(account.provider) + R"(,"status":)" +
           core::text::quote_json_string(account.status) + R"(,"planType":)" +
           optional_string_json(account.plan_type) + R"(,"quotaPrimaryPercent":)" +
           optional_int_json(account.quota_primary_percent) + R"(,"quotaSecondaryPercent":)" +
           optional_int_json(account.quota_secondary_percent) + "}";
}

} // namespace

void wire_accounts_routes(uWS::App& app) {
    app.get("/api/accounts", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        const auto response = controllers::list_accounts();
        if (response.status == 200) {
            std::string body = R"({"accounts":[)";
            for (std::size_t i = 0; i < response.accounts.size(); ++i) {
                if (i > 0) {
                    body.push_back(',');
                }
                body += account_json(response.accounts[i]);
            }
            body += "]}";
            http::write_json(res, 200, body);
            return;
        }
        http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
    });

    app.post("/api/accounts/import", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        http::read_request_body(res, [res](std::string body) mutable {
            const auto parsed = parse_json_object(body);
            if (!parsed.has_value()) {
                http::write_json(res, 400, dashboard_error_json("invalid_account_import", "Invalid JSON payload"));
                return;
            }
            const auto email = json_string(*parsed, "email").value_or("");
            const auto provider = json_string(*parsed, "provider").value_or("");
            const auto response = controllers::import_account(email, provider);
            if (response.status == 201) {
                http::write_json(res, 201, account_json(response.account));
                return;
            }
            http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
        });
    });

    app.post("/api/accounts/:account_id/pause", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        const auto account_id = http::decode_percent_escapes(http::to_string_safe(req->getParameter(0)));
        const auto response = controllers::pause_account(account_id);
        if (response.status == 200) {
            http::write_json(res, 200, R"({"status":"paused"})");
            return;
        }
        http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
    });

    app.post("/api/accounts/:account_id/reactivate", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        const auto account_id = http::decode_percent_escapes(http::to_string_safe(req->getParameter(0)));
        const auto response = controllers::reactivate_account(account_id);
        if (response.status == 200) {
            http::write_json(res, 200, R"({"status":"reactivated"})");
            return;
        }
        http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
    });

    app.del("/api/accounts/:account_id", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        const auto account_id = http::decode_percent_escapes(http::to_string_safe(req->getParameter(0)));
        const auto response = controllers::delete_account(account_id);
        if (response.status == 200) {
            http::write_json(res, 200, R"({"status":"deleted"})");
            return;
        }
        http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
    });

    app.post("/api/accounts/:account_id/refresh-usage", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        const auto account_id = http::decode_percent_escapes(http::to_string_safe(req->getParameter(0)));
        const auto response = controllers::refresh_account_usage(account_id);
        if (response.status == 200) {
            http::write_json(res, 200, account_json(response.account));
            return;
        }
        http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
    });
}

} // namespace tightrope::server::internal::admin
