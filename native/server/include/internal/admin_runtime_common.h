#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glaze/glaze.hpp>
#include <uwebsockets/App.h>

#include "openai/upstream_headers.h"

namespace tightrope::server::internal::admin {

using Json = glz::generic;

std::optional<std::string> json_string(const Json::object_t& payload, std::string_view key);
std::optional<bool> json_bool(const Json::object_t& payload, std::string_view key);
std::optional<std::int64_t> json_int64(const Json::object_t& payload, std::string_view key);
std::optional<double> json_double(const Json::object_t& payload, std::string_view key);
std::vector<std::string> json_string_array(const Json::object_t& payload, std::string_view key);
std::optional<Json::object_t> parse_json_object(const std::string& body);

std::string bool_json(bool value);
std::string number_json(double value);
std::string optional_string_json(const std::optional<std::string>& value);
std::string dashboard_error_json(std::string code, std::string message);

std::optional<std::string> cookie_value(const proxy::openai::HeaderMap& headers, std::string_view key);
std::string session_cookie_header(std::string_view session_id);
std::string clear_session_cookie_header();

void write_json_with_cookie(
    uWS::HttpResponse<false>* res,
    int status,
    const std::string& body,
    const std::optional<std::string>& cookie_header = std::nullopt
);

std::string request_host(uWS::HttpRequest* req);

} // namespace tightrope::server::internal::admin
