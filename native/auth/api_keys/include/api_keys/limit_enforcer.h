#pragma once
// Per-key limit checking

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tightrope::auth::api_keys {

[[nodiscard]] bool is_supported_limit_type(std::string_view limit_type) noexcept;
[[nodiscard]] std::optional<std::int64_t> window_to_period_seconds(std::string_view limit_window) noexcept;
[[nodiscard]] std::optional<std::string> period_seconds_to_window(std::int64_t period_seconds) noexcept;
[[nodiscard]] std::optional<std::string> normalize_reasoning_effort(std::string_view value) noexcept;
[[nodiscard]] std::vector<std::string> normalize_allowed_models(const std::optional<std::vector<std::string>>& models);
[[nodiscard]] std::optional<std::string> serialize_allowed_models(const std::vector<std::string>& models);
[[nodiscard]] std::vector<std::string> deserialize_allowed_models(const std::optional<std::string>& encoded);
[[nodiscard]] bool validate_model_enforcement(
    const std::optional<std::string>& enforced_model,
    const std::vector<std::string>& allowed_models
) noexcept;

} // namespace tightrope::auth::api_keys
