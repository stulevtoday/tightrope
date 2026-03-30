#pragma once

#include <string>
#include <string_view>

#include "model_registry.h"
#include "upstream_headers.h"

namespace tightrope::proxy::openai {

std::string configured_stream_transport(std::string_view transport, std::string_view transport_override);
bool is_native_codex_originator(std::string_view originator);
bool has_native_codex_transport_headers(const HeaderMap& headers);

std::string resolve_stream_transport(
    std::string_view transport,
    std::string_view transport_override,
    std::string_view model,
    const HeaderMap& headers,
    const ModelRegistry& registry
);

} // namespace tightrope::proxy::openai
