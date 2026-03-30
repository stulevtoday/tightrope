#pragma once
// bcrypt password verification

#include <optional>
#include <string>
#include <string_view>

namespace tightrope::auth::dashboard {

[[nodiscard]] std::optional<std::string> hash_password(std::string_view password) noexcept;
[[nodiscard]] bool verify_password(std::string_view password, std::string_view password_hash) noexcept;

} // namespace tightrope::auth::dashboard
