#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sqlite3.h>

#include "openai/upstream_headers.h"

namespace tightrope::server::middleware {

struct AuthDecision {
    bool allow = true;
    int status = 200;
    std::string body;
};

struct ApiKeyModelPolicy {
    std::optional<std::string> key_id;
    std::vector<std::string> allowed_models;
    std::optional<std::string> enforced_model;
    std::optional<std::string> enforced_reasoning_effort;
};

struct ApiKeyRequestPolicyDecision {
    bool allow = true;
    int status = 200;
    std::string error_code;
    std::string error_message;
    std::string error_type;
    std::string error_body;
};

[[nodiscard]] bool is_proxy_api_key_protected_path(std::string_view path) noexcept;
[[nodiscard]] bool is_codex_usage_identity_path(std::string_view path) noexcept;

[[nodiscard]] AuthDecision validate_proxy_api_key_request(
    sqlite3* db,
    std::string_view path,
    const proxy::openai::HeaderMap& headers
) noexcept;

[[nodiscard]] AuthDecision validate_codex_usage_identity_request(
    sqlite3* db,
    std::string_view path,
    const proxy::openai::HeaderMap& headers
) noexcept;

[[nodiscard]] ApiKeyModelPolicy resolve_api_key_model_policy(
    sqlite3* db,
    const proxy::openai::HeaderMap& headers
) noexcept;

[[nodiscard]] ApiKeyRequestPolicyDecision enforce_api_key_request_policy(
    const ApiKeyModelPolicy& policy,
    std::string& request_body
) noexcept;

} // namespace tightrope::server::middleware
