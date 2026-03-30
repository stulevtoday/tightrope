#include "error_envelope.h"

#include "text/json_escape.h"

namespace tightrope::proxy::openai {

std::string build_error_envelope(
    const std::string& code,
    const std::string& message,
    const std::string& type,
    const std::string_view param
) {
    std::string payload = "{\"error\":{\"message\":\"" + core::text::escape_json_string(message) + "\",\"type\":\"" +
                          core::text::escape_json_string(type) + "\",\"code\":\"" + core::text::escape_json_string(code) + "\"";
    if (!param.empty()) {
        payload += ",\"param\":\"";
        payload += core::text::escape_json_string(std::string(param));
        payload += "\"";
    }
    payload += "}}";
    return payload;
}

} // namespace tightrope::proxy::openai
