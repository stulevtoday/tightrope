#include "request_id.h"

#include <string>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "text/ascii.h"

namespace tightrope::server::middleware {

namespace {

std::string trim_copy(std::string value) {
    const auto trimmed = core::text::trim_ascii(std::string_view(value));
    return std::string(trimmed);
}

std::string find_header_case_insensitive(
    const proxy::openai::HeaderMap& headers,
    const std::string_view header_name
) {
    for (const auto& [key, value] : headers) {
        if (core::text::equals_case_insensitive(key, header_name)) {
            return trim_copy(value);
        }
    }
    return {};
}

std::string generate_request_id() {
    static thread_local boost::uuids::random_generator generator;
    return boost::uuids::to_string(generator());
}

} // namespace

std::string request_id_from_headers(const proxy::openai::HeaderMap& headers) {
    auto request_id = find_header_case_insensitive(headers, "x-request-id");
    if (!request_id.empty()) {
        return request_id;
    }
    request_id = find_header_case_insensitive(headers, "request-id");
    if (!request_id.empty()) {
        return request_id;
    }
    return {};
}

std::string ensure_request_id(proxy::openai::HeaderMap& headers) {
    auto request_id = request_id_from_headers(headers);
    if (request_id.empty()) {
        request_id = generate_request_id();
    }
    headers["x-request-id"] = request_id;
    return request_id;
}

} // namespace tightrope::server::middleware
