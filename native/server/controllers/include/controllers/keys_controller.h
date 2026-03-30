#pragma once
// keys API controller

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sqlite3.h>

namespace tightrope::server::controllers {

struct ApiKeyLimitRuleInput {
    std::string limit_type;
    std::string limit_window;
    double max_value = 0.0;
    std::optional<std::string> model_filter;
};

struct ApiKeyLimitPayload {
    std::int64_t id = 0;
    std::string limit_type;
    std::string limit_window;
    double max_value = 0.0;
    double current_value = 0.0;
    std::optional<std::string> model_filter;
};

struct ApiKeyPayload {
    std::string key_id;
    std::string name;
    std::string key_prefix;
    bool is_active = true;
    std::vector<std::string> allowed_models;
    std::optional<std::string> enforced_model;
    std::optional<std::string> enforced_reasoning_effort;
    std::optional<std::string> expires_at;
    std::string created_at;
    std::optional<std::string> last_used_at;
    std::vector<ApiKeyLimitPayload> limits;
};

struct ApiKeyCreateRequest {
    std::string name;
    std::optional<std::vector<std::string>> allowed_models;
    std::optional<std::string> enforced_model;
    std::optional<std::string> enforced_reasoning_effort;
    std::optional<std::string> expires_at;
    std::vector<ApiKeyLimitRuleInput> limits;
};

struct ApiKeyUpdateRequest {
    std::optional<std::string> name;
    std::optional<std::vector<std::string>> allowed_models;
    std::optional<std::string> enforced_model;
    std::optional<std::string> enforced_reasoning_effort;
    std::optional<bool> is_active;
    std::optional<std::string> expires_at;
    std::optional<std::vector<ApiKeyLimitRuleInput>> limits;
};

struct ApiKeyMutationResponse {
    int status = 500;
    std::string code;
    std::string message;
    std::string key;
    ApiKeyPayload api_key;
};

struct ApiKeyDeleteResponse {
    int status = 500;
    std::string code;
    std::string message;
};

struct ApiKeyListResponse {
    int status = 500;
    std::string code;
    std::string message;
    std::vector<ApiKeyPayload> items;
};

[[nodiscard]] ApiKeyMutationResponse create_api_key(const ApiKeyCreateRequest& request, sqlite3* db = nullptr);
[[nodiscard]] ApiKeyListResponse list_api_keys(sqlite3* db = nullptr);
[[nodiscard]] ApiKeyMutationResponse
update_api_key(std::string_view key_id, const ApiKeyUpdateRequest& request, sqlite3* db = nullptr);
[[nodiscard]] ApiKeyMutationResponse regenerate_api_key(std::string_view key_id, sqlite3* db = nullptr);
[[nodiscard]] ApiKeyDeleteResponse delete_api_key(std::string_view key_id, sqlite3* db = nullptr);

} // namespace tightrope::server::controllers
