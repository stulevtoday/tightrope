#pragma once

#include <string>

#include <sqlite3.h>

#include "openai/upstream_headers.h"

namespace tightrope::server::controllers {

struct CodexUsageResponse {
    int status = 500;
    std::string body;
};

[[nodiscard]] CodexUsageResponse get_codex_usage(const proxy::openai::HeaderMap& headers = {}, sqlite3* db = nullptr);

} // namespace tightrope::server::controllers
