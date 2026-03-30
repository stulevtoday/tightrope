#pragma once

#include <string>

// Request/response shape normalization

namespace tightrope::proxy::openai {

struct NormalizedRequest {
    std::string body;
};

NormalizedRequest normalize_request(const std::string& raw_request_body);
NormalizedRequest normalize_compact_request(const std::string& raw_request_body);

} // namespace tightrope::proxy::openai
