#pragma once

#include <string>
#include <string_view>

// OpenAI-compatible error formatting

namespace tightrope::proxy::openai {

std::string build_error_envelope(
    const std::string& code,
    const std::string& message,
    const std::string& type = "invalid_request_error",
    std::string_view param = ""
);

} // namespace tightrope::proxy::openai
