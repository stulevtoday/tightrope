#include "internal/http_runtime_utils.h"

#include <string>

#include "text/ascii.h"

namespace tightrope::server::internal::http {

namespace {

std::string status_reason(const int status) {
    switch (status) {
    case 101:
        return "Switching Protocols";
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 202:
        return "Accepted";
    case 204:
        return "No Content";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 409:
        return "Conflict";
    case 422:
        return "Unprocessable Entity";
    case 429:
        return "Too Many Requests";
    case 500:
        return "Internal Server Error";
    case 502:
        return "Bad Gateway";
    case 503:
        return "Service Unavailable";
    default:
        return "Unknown";
    }
}

std::string status_line_internal(const int status) {
    return std::to_string(status) + " " + status_reason(status);
}

int parse_hex_nibble(const char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

void write_extra_headers(
    uWS::HttpResponse<false>* res,
    const proxy::openai::HeaderMap* extra_headers
) {
    if (extra_headers == nullptr) {
        return;
    }
    for (const auto& [key, value] : *extra_headers) {
        if (!key.empty()) {
            res->writeHeader(key, value);
        }
    }
}

} // namespace

std::string to_string_safe(const std::string_view value) {
    if (value.data() == nullptr) {
        return {};
    }
    return std::string(value);
}

std::optional<std::string> maybe_string(const std::string_view value) {
    if (value.data() == nullptr || value.empty()) {
        return std::nullopt;
    }
    return std::string(value);
}

std::string status_line(const int status) {
    return status_line_internal(status);
}

std::string decode_percent_escapes(const std::string_view raw) {
    std::string decoded;
    decoded.reserve(raw.size());
    for (std::size_t index = 0; index < raw.size(); ++index) {
        const char ch = raw[index];
        if (ch != '%' || index + 2 >= raw.size()) {
            decoded.push_back(ch);
            continue;
        }
        const int upper = parse_hex_nibble(raw[index + 1]);
        const int lower = parse_hex_nibble(raw[index + 2]);
        if (upper < 0 || lower < 0) {
            decoded.push_back(ch);
            continue;
        }
        decoded.push_back(static_cast<char>((upper << 4) | lower));
        index += 2;
    }
    return decoded;
}

proxy::openai::HeaderMap request_headers(uWS::HttpRequest* req) {
    proxy::openai::HeaderMap headers;
    for (auto [key, value] : *req) {
        headers.emplace(std::string(key), std::string(value));
    }
    return headers;
}

bool header_contains(
    const proxy::openai::HeaderMap& headers,
    const std::string_view key,
    const std::string_view needle
) {
    const auto key_lower = core::text::to_lower_ascii(key);
    const auto needle_lower = core::text::to_lower_ascii(needle);
    for (const auto& [header_key, header_value] : headers) {
        if (core::text::to_lower_ascii(header_key) != key_lower) {
            continue;
        }
        return core::text::to_lower_ascii(header_value).find(needle_lower) != std::string::npos;
    }
    return false;
}

std::optional<std::string_view> socket_ip(uWS::HttpResponse<false>* res) {
    const auto remote = res->getRemoteAddressAsText();
    if (remote.data() == nullptr || remote.empty()) {
        return std::nullopt;
    }
    return remote;
}

void write_http(
    uWS::HttpResponse<false>* res,
    const int status,
    const std::string_view content_type,
    const std::string_view body,
    const std::string_view request_id,
    const proxy::openai::HeaderMap* extra_headers
) {
    const auto status_text = status_line_internal(status);
    res->writeStatus(status_text);
    if (!content_type.empty()) {
        res->writeHeader("Content-Type", content_type);
    }
    if (!request_id.empty()) {
        res->writeHeader("x-request-id", request_id);
    }
    write_extra_headers(res, extra_headers);
    res->writeHeader("Connection", "close");
    res->end(body);
}

void write_json(
    uWS::HttpResponse<false>* res,
    const int status,
    const std::string& body,
    const std::string_view request_id,
    const proxy::openai::HeaderMap* extra_headers
) {
    write_http(res, status, "application/json", body, request_id, extra_headers);
}

void write_sse(
    uWS::HttpResponse<false>* res,
    const int status,
    const std::vector<std::string>& events,
    const std::string_view request_id,
    const proxy::openai::HeaderMap* extra_headers
) {
    const auto status_text = status_line_internal(status);
    res->writeStatus(status_text);
    res->writeHeader("Content-Type", "text/event-stream");
    res->writeHeader("Cache-Control", "no-cache");
    if (!request_id.empty()) {
        res->writeHeader("x-request-id", request_id);
    }
    write_extra_headers(res, extra_headers);
    res->writeHeader("Connection", "close");

    for (const auto& event : events) {
        res->write("data: ");
        res->write(event);
        res->write("\n\n");
    }
    res->end();
}

} // namespace tightrope::server::internal::http
