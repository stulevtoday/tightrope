#pragma once

#include <cstddef>
#include <string>

#include "openai/upstream_headers.h"

namespace tightrope::server::middleware {

struct DecompressionResult {
    bool ok = true;
    int status = 200;
    std::string body;
    std::string error_body;
    proxy::openai::HeaderMap headers;
};

[[nodiscard]] DecompressionResult
decompress_request_body(std::string body, proxy::openai::HeaderMap headers, std::size_t max_size = 32u * 1024u * 1024u);

} // namespace tightrope::server::middleware
