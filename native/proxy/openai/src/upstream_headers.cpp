#include "upstream_headers.h"

#include <array>
#include <string>
#include <string_view>
#include <unordered_set>

#include "text/ascii.h"

namespace tightrope::proxy::openai {

namespace {

constexpr std::array<std::string_view, 8> kIgnoredInboundHeaders = {
    "authorization",
    "chatgpt-account-id",
    "content-length",
    "host",
    "forwarded",
    "x-real-ip",
    "true-client-ip",
    "x-forwarded-for", // explicit fast path, prefix handled below
};

constexpr std::array<std::string_view, 11> kHopByHopHeaderNames = {
    "accept",
    "connection",
    "content-type",
    "keep-alive",
    "proxy-authenticate",
    "proxy-authorization",
    "proxy-connection",
    "te",
    "trailer",
    "transfer-encoding",
    "upgrade",
};

bool is_ignored_inbound_header(const std::string_view name_lower) {
    for (const auto ignored : kIgnoredInboundHeaders) {
        if (name_lower == ignored) {
            return true;
        }
    }
    return false;
}

std::unordered_set<std::string> collect_connection_tokens(const HeaderMap& inbound) {
    std::unordered_set<std::string> tokens;
    for (const auto& [key, value] : inbound) {
        if (core::text::to_lower_ascii(key) != "connection") {
            continue;
        }
        std::size_t start = 0;
        while (start <= value.size()) {
            const auto comma = value.find(',', start);
            const auto piece =
                comma == std::string::npos ? value.substr(start) : value.substr(start, comma - start);
            const auto trimmed = core::text::trim_ascii(piece);
            if (!trimmed.empty()) {
                tokens.insert(core::text::to_lower_ascii(trimmed));
            }
            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }
    }
    return tokens;
}

} // namespace

bool should_drop_inbound_header(const std::string_view name) {
    const auto lower = core::text::to_lower_ascii(name);
    if (is_ignored_inbound_header(lower)) {
        return true;
    }
    return core::text::starts_with(lower, "x-forwarded-") || core::text::starts_with(lower, "cf-");
}

HeaderMap filter_inbound_headers(const HeaderMap& headers) {
    HeaderMap filtered;
    for (const auto& [key, value] : headers) {
        if (should_drop_inbound_header(key)) {
            continue;
        }
        filtered.emplace(key, value);
    }
    return filtered;
}

HeaderMap build_upstream_headers(
    const HeaderMap& inbound,
    const std::string_view access_token,
    const std::string_view account_id,
    const std::string_view accept,
    const std::string_view request_id
) {
    HeaderMap headers = inbound;
    if (!core::text::has_key_case_insensitive(headers, "x-request-id") &&
        !core::text::has_key_case_insensitive(headers, "request-id") &&
        !request_id.empty()) {
        headers["x-request-id"] = std::string(request_id);
    }

    headers["Authorization"] = "Bearer " + std::string(access_token);
    headers["Accept"] = std::string(accept);
    headers["Content-Type"] = "application/json";
    if (!account_id.empty()) {
        headers["chatgpt-account-id"] = std::string(account_id);
    }
    return headers;
}

HeaderMap build_upstream_transcribe_headers(
    const HeaderMap& inbound,
    const std::string_view access_token,
    const std::string_view account_id
) {
    HeaderMap headers;
    headers["Authorization"] = "Bearer " + std::string(access_token);
    if (!account_id.empty()) {
        headers["chatgpt-account-id"] = std::string(account_id);
    }

    for (const auto& [key, value] : inbound) {
        const auto lower = core::text::to_lower_ascii(key);
        if (lower == "user-agent" || core::text::starts_with(lower, "x-openai-") ||
            core::text::starts_with(lower, "x-codex-")) {
            headers[key] = value;
        }
    }

    return headers;
}

HeaderMap build_upstream_websocket_headers(
    const HeaderMap& inbound,
    const std::string_view access_token,
    const std::string_view account_id,
    const std::string_view request_id
) {
    auto blocked = collect_connection_tokens(inbound);
    for (const auto hop : kHopByHopHeaderNames) {
        blocked.insert(std::string(hop));
    }

    HeaderMap headers;
    for (const auto& [key, value] : inbound) {
        const auto lower = core::text::to_lower_ascii(key);
        if (blocked.find(lower) != blocked.end()) {
            continue;
        }
        headers.emplace(key, value);
    }

    if (!core::text::has_key_case_insensitive(headers, "x-request-id") &&
        !core::text::has_key_case_insensitive(headers, "request-id") &&
        !request_id.empty()) {
        headers["x-request-id"] = std::string(request_id);
    }
    headers["Authorization"] = "Bearer " + std::string(access_token);
    if (!account_id.empty()) {
        headers["chatgpt-account-id"] = std::string(account_id);
    }
    return headers;
}

} // namespace tightrope::proxy::openai
