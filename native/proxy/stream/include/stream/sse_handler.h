#pragma once
// sse stream handler

#include <string>
#include <string_view>
#include <vector>

namespace tightrope::proxy::stream {

std::vector<std::string> build_responses_sse_success(std::string_view route);
std::vector<std::string> build_responses_sse_failure(
    std::string_view error_code,
    std::string_view error_message
);

} // namespace tightrope::proxy::stream
