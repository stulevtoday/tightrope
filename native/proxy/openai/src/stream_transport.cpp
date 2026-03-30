#include "stream_transport.h"

#include <array>

#include "text/ascii.h"

namespace tightrope::proxy::openai {

namespace {

constexpr std::array<std::string_view, 6> kNativeCodexOriginators = {
    "Codex Desktop",
    "codex_atlas",
    "codex_chatgpt_desktop",
    "codex_cli_rs",
    "codex_exec",
    "codex_vscode",
};

constexpr std::array<std::string_view, 3> kNativeCodexStreamHeaderKeys = {
    "x-codex-turn-state",
    "x-codex-turn-metadata",
    "x-codex-beta-features",
};

} // namespace

std::string configured_stream_transport(std::string_view transport, std::string_view transport_override) {
    return transport_override.empty() ? std::string(transport) : std::string(transport_override);
}

bool is_native_codex_originator(std::string_view originator) {
    const auto stripped = core::text::trim_ascii(originator);
    if (stripped.empty()) {
        return false;
    }
    for (const auto candidate : kNativeCodexOriginators) {
        if (stripped == candidate) {
            return true;
        }
    }
    return false;
}

bool has_native_codex_transport_headers(const HeaderMap& headers) {
    HeaderMap normalized;
    normalized.reserve(headers.size());
    for (const auto& [key, value] : headers) {
        normalized.emplace(core::text::to_lower_ascii(key), value);
    }

    const auto originator = normalized.find("originator");
    if (originator != normalized.end() && is_native_codex_originator(originator->second)) {
        return true;
    }
    for (const auto key : kNativeCodexStreamHeaderKeys) {
        if (normalized.find(std::string(key)) != normalized.end()) {
            return true;
        }
    }
    return false;
}

std::string resolve_stream_transport(
    const std::string_view transport,
    const std::string_view transport_override,
    const std::string_view model,
    const HeaderMap& headers,
    const ModelRegistry& registry
) {
    const auto configured = configured_stream_transport(transport, transport_override);
    if (configured == "websocket") {
        return "websocket";
    }
    if (configured == "http") {
        return "http";
    }

    if (has_native_codex_transport_headers(headers)) {
        return "websocket";
    }

    if (registry.prefers_websockets(model)) {
        return "websocket";
    }
    return "http";
}

} // namespace tightrope::proxy::openai
