#include "decompression.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

#include "text/ascii.h"
#include "text/json_escape.h"

namespace tightrope::server::middleware {

namespace {

std::string dashboard_error_json(const std::string_view code, const std::string_view message) {
    const std::string code_str(code);
    const std::string message_str(message);
    return std::string(R"({"error":{"code":)") + core::text::quote_json_string(code_str) + R"(,"message":)" +
           core::text::quote_json_string(message_str) + "}}";
}

std::string find_header_case_insensitive(
    const proxy::openai::HeaderMap& headers,
    const std::string_view header_name
) {
    for (const auto& [key, value] : headers) {
        if (core::text::equals_case_insensitive(key, header_name)) {
            return value;
        }
    }
    return {};
}

std::vector<std::string> parse_content_encodings(const std::string_view value) {
    std::vector<std::string> encodings;
    std::size_t start = 0;
    while (start < value.size()) {
        const auto comma = value.find(',', start);
        const auto end = comma == std::string_view::npos ? value.size() : comma;
        const auto token = core::text::trim_ascii(value.substr(start, end - start));
        if (!token.empty()) {
            encodings.push_back(core::text::to_lower_ascii(token));
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return encodings;
}

std::string inflate_with_window_bits(const std::string& input, const int window_bits, const std::size_t max_size) {
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    if (inflateInit2(&stream, window_bits) != Z_OK) {
        throw std::runtime_error("inflate init failed");
    }

    std::string output;
    output.reserve(std::min<std::size_t>(input.size() * 2u, max_size));
    std::array<char, 64 * 1024> buffer{};

    int rc = Z_OK;
    while (rc == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        rc = inflate(&stream, Z_NO_FLUSH);
        const auto produced = buffer.size() - stream.avail_out;
        if (produced > 0) {
            if (output.size() + produced > max_size) {
                inflateEnd(&stream);
                throw std::length_error("decompressed body exceeds max size");
            }
            output.append(buffer.data(), produced);
        }
    }

    inflateEnd(&stream);
    if (rc != Z_STREAM_END) {
        throw std::runtime_error("inflate failed");
    }
    return output;
}

std::string decompress_one(const std::string& input, const std::string_view encoding, const std::size_t max_size) {
    if (encoding == "identity") {
        return input;
    }
    if (encoding == "gzip") {
        return inflate_with_window_bits(input, 16 + MAX_WBITS, max_size);
    }
    if (encoding == "deflate") {
        return inflate_with_window_bits(input, MAX_WBITS, max_size);
    }
    throw std::invalid_argument("unsupported content encoding");
}

proxy::openai::HeaderMap strip_content_encoding_headers(const proxy::openai::HeaderMap& headers, const std::size_t body_size) {
    proxy::openai::HeaderMap rewritten;
    rewritten.reserve(headers.size() + 1);
    for (const auto& [key, value] : headers) {
        if (core::text::equals_case_insensitive(key, "content-encoding") ||
            core::text::equals_case_insensitive(key, "content-length")) {
            continue;
        }
        rewritten.emplace(key, value);
    }
    rewritten["content-length"] = std::to_string(body_size);
    return rewritten;
}

} // namespace

DecompressionResult decompress_request_body(std::string body, proxy::openai::HeaderMap headers, const std::size_t max_size) {
    const auto content_encoding = find_header_case_insensitive(headers, "content-encoding");
    if (content_encoding.empty()) {
        return {
            .ok = true,
            .status = 200,
            .body = std::move(body),
            .headers = std::move(headers),
        };
    }

    const auto encodings = parse_content_encodings(content_encoding);
    if (encodings.empty()) {
        return {
            .ok = true,
            .status = 200,
            .body = std::move(body),
            .headers = std::move(headers),
        };
    }

    try {
        std::string decompressed = std::move(body);
        for (auto it = encodings.rbegin(); it != encodings.rend(); ++it) {
            decompressed = decompress_one(decompressed, *it, max_size);
        }
        return {
            .ok = true,
            .status = 200,
            .body = std::move(decompressed),
            .headers = strip_content_encoding_headers(headers, decompressed.size()),
        };
    } catch (const std::invalid_argument&) {
        return {
            .ok = false,
            .status = 400,
            .error_body = dashboard_error_json("invalid_request", "Unsupported Content-Encoding"),
        };
    } catch (const std::length_error&) {
        return {
            .ok = false,
            .status = 413,
            .error_body = dashboard_error_json("payload_too_large", "Request body exceeds the maximum allowed size"),
        };
    } catch (...) {
        return {
            .ok = false,
            .status = 400,
            .error_body = dashboard_error_json(
                "invalid_request",
                "Request body is compressed but could not be decompressed"
            ),
        };
    }
}

} // namespace tightrope::server::middleware
