#include "internal/firewall_runtime.h"

#include <optional>
#include <string>

#include <glaze/glaze.hpp>

#include "internal/http_runtime_utils.h"
#include "text/json_escape.h"
#include "controllers/controller_db.h"
#include "controllers/firewall_controller.h"
#include "middleware/firewall_filter.h"

namespace tightrope::server::internal::firewall {

namespace {

using Json = glz::generic;

std::optional<std::string> json_string(const Json::object_t& payload, const std::string_view key) {
    const auto it = payload.find(std::string(key));
    if (it == payload.end() || !it->second.is_string()) {
        return std::nullopt;
    }
    return it->second.get_string();
}

std::string firewall_error_json(std::string code, std::string message) {
    if (code.empty()) {
        code = "firewall_request_failed";
    }
    if (message.empty()) {
        message = "Firewall request failed";
    }
    return std::string(R"({"error":{"code":)") + core::text::quote_json_string(code) + R"(,"message":)" +
           core::text::quote_json_string(message) + "}}";
}

std::string firewall_entry_json(const controllers::FirewallIpEntryPayload& entry) {
    return std::string(R"({"ipAddress":)") + core::text::quote_json_string(entry.ip_address) + R"(,"createdAt":)" +
           core::text::quote_json_string(entry.created_at) + "}";
}

std::string firewall_list_json(const controllers::FirewallListResponse& response) {
    std::string body = std::string(R"({"mode":)") + core::text::quote_json_string(response.mode) + R"(,"entries":[)";
    for (std::size_t index = 0; index < response.entries.size(); ++index) {
        if (index > 0) {
            body.push_back(',');
        }
        body += firewall_entry_json(response.entries[index]);
    }
    body += "]}";
    return body;
}

std::optional<std::string> parse_firewall_ip_address(const std::string& body) {
    if (body.empty()) {
        return std::nullopt;
    }
    Json payload;
    if (const auto ec = glz::read_json(payload, body); ec || !payload.is_object()) {
        return std::nullopt;
    }
    const auto& object = payload.get_object();
    if (const auto ip_address = json_string(object, "ipAddress"); ip_address.has_value()) {
        return ip_address;
    }
    return json_string(object, "ip_address");
}

} // namespace

bool allow_request_or_write_denial(
    uWS::HttpResponse<false>* res,
    const std::string_view path,
    const proxy::openai::HeaderMap& headers
) {
    auto controller_db = controllers::open_controller_db();
    if (controller_db.db == nullptr) {
        http::write_json(res, 500, firewall_error_json("db_unavailable", "Database unavailable"));
        return false;
    }

    const auto decision = middleware::evaluate_firewall_request(controller_db.db, path, headers, http::socket_ip(res));
    if (decision.allow) {
        return true;
    }
    http::write_json(res, decision.status, decision.body);
    return false;
}

void wire_routes(uWS::App& app) {
    app.get("/api/firewall/ips", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        const auto response = controllers::list_firewall_ips();
        if (response.status == 200) {
            http::write_json(res, 200, firewall_list_json(response));
            return;
        }
        http::write_json(res, response.status, firewall_error_json(response.code, response.message));
    });

    app.post("/api/firewall/ips", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        http::read_request_body(res, [res](std::string body) mutable {
            const auto ip_address = parse_firewall_ip_address(body);
            if (!ip_address.has_value()) {
                http::write_json(res, 400, firewall_error_json("invalid_ip", "Invalid IP address"));
                return;
            }

            const auto response = controllers::add_firewall_ip(*ip_address);
            if (response.status == 200) {
                http::write_json(res, 200, firewall_entry_json(response.entry));
                return;
            }
            http::write_json(res, response.status, firewall_error_json(response.code, response.message));
        });
    });

    app.del("/api/firewall/ips/:ip_address", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        const auto ip_address = http::decode_percent_escapes(http::to_string_safe(req->getParameter(0)));
        const auto response = controllers::delete_firewall_ip(ip_address);
        if (response.status == 200) {
            const auto result = response.result.empty() ? std::string("deleted") : response.result;
            http::write_json(
                res,
                200,
                std::string(R"({"status":)") + core::text::quote_json_string(result) + "}"
            );
            return;
        }
        http::write_json(res, response.status, firewall_error_json(response.code, response.message));
    });
}

} // namespace tightrope::server::internal::firewall

