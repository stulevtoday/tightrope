#include "internal/admin_runtime_common.h"

#include <sstream>

#include "internal/http_runtime_utils.h"
#include "text/json_escape.h"

namespace tightrope::server::internal::admin {

namespace {

constexpr std::string_view kDashboardSessionCookie = "tightrope_dashboard_session";

} // namespace

std::optional<std::string> json_string(const Json::object_t& payload, const std::string_view key) {
    const auto it = payload.find(std::string(key));
    if (it == payload.end() || !it->second.is_string()) {
        return std::nullopt;
    }
    return it->second.get_string();
}

std::optional<bool> json_bool(const Json::object_t& payload, const std::string_view key) {
    const auto it = payload.find(std::string(key));
    if (it == payload.end() || !it->second.is_boolean()) {
        return std::nullopt;
    }
    return it->second.get_boolean();
}

std::optional<std::int64_t> json_int64(const Json::object_t& payload, const std::string_view key) {
    const auto it = payload.find(std::string(key));
    if (it == payload.end() || !it->second.is_number()) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(it->second.get_number());
}

std::optional<double> json_double(const Json::object_t& payload, const std::string_view key) {
    const auto it = payload.find(std::string(key));
    if (it == payload.end() || !it->second.is_number()) {
        return std::nullopt;
    }
    return it->second.get_number();
}

std::vector<std::string> json_string_array(const Json::object_t& payload, const std::string_view key) {
    std::vector<std::string> values;
    const auto it = payload.find(std::string(key));
    if (it == payload.end() || !it->second.is_array()) {
        return values;
    }
    for (const auto& item : it->second.get_array()) {
        if (item.is_string()) {
            values.push_back(item.get_string());
        }
    }
    return values;
}

std::optional<Json::object_t> parse_json_object(const std::string& body) {
    if (body.empty()) {
        return std::nullopt;
    }
    Json payload;
    if (const auto ec = glz::read_json(payload, body); ec || !payload.is_object()) {
        return std::nullopt;
    }
    return payload.get_object();
}

std::string bool_json(const bool value) {
    return value ? "true" : "false";
}

std::string number_json(const double value) {
    std::ostringstream stream;
    stream.precision(15);
    stream << value;
    return stream.str();
}

std::string optional_string_json(const std::optional<std::string>& value) {
    return value.has_value() ? core::text::quote_json_string(*value) : "null";
}

std::string dashboard_error_json(std::string code, std::string message) {
    if (code.empty()) {
        code = "request_failed";
    }
    if (message.empty()) {
        message = "Request failed";
    }
    return std::string(R"({"error":{"code":)") + core::text::quote_json_string(code) + R"(,"message":)" +
           core::text::quote_json_string(message) + "}}";
}

std::optional<std::string> cookie_value(const proxy::openai::HeaderMap& headers, const std::string_view key) {
    auto it = headers.find("cookie");
    if (it == headers.end()) {
        it = headers.find("Cookie");
        if (it == headers.end()) {
            return std::nullopt;
        }
    }

    std::string_view cookie_header = it->second;
    std::size_t start = 0;
    while (start < cookie_header.size()) {
        const auto end = cookie_header.find(';', start);
        const auto token = cookie_header.substr(
            start,
            end == std::string_view::npos ? std::string_view::npos : end - start
        );
        const auto eq = token.find('=');
        if (eq != std::string_view::npos) {
            auto name = token.substr(0, eq);
            while (!name.empty() && name.front() == ' ') {
                name.remove_prefix(1);
            }
            if (name == key) {
                return std::string(token.substr(eq + 1));
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return std::nullopt;
}

std::string session_cookie_header(const std::string_view session_id) {
    return std::string(kDashboardSessionCookie) + "=" + std::string(session_id) +
           "; Path=/; HttpOnly; SameSite=Lax; Max-Age=43200";
}

std::string clear_session_cookie_header() {
    return std::string(kDashboardSessionCookie) + "=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0";
}

void write_json_with_cookie(
    uWS::HttpResponse<false>* res,
    const int status,
    const std::string& body,
    const std::optional<std::string>& cookie_header
) {
    res->writeStatus(http::status_line(status));
    res->writeHeader("Content-Type", "application/json");
    if (cookie_header.has_value()) {
        res->writeHeader("Set-Cookie", *cookie_header);
    }
    res->writeHeader("Connection", "close");
    res->end(body);
}

std::string request_host(uWS::HttpRequest* req) {
    auto host = http::to_string_safe(req->getHeader("host"));
    if (host.empty()) {
        return {};
    }
    if (host.front() == '[') {
        const auto closing = host.find(']');
        if (closing != std::string::npos) {
            return host.substr(1, closing - 1);
        }
    }
    const auto colon = host.find(':');
    if (colon == std::string::npos) {
        return host;
    }
    return host.substr(0, colon);
}

} // namespace tightrope::server::internal::admin
