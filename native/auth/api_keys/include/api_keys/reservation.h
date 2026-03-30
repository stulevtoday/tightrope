#pragma once
// Usage reservation/settlement

#include <string>
#include <string_view>

namespace tightrope::auth::api_keys {

[[nodiscard]] std::string generate_reservation_request_id();
[[nodiscard]] bool is_valid_status_transition(std::string_view current_status, std::string_view next_status) noexcept;
[[nodiscard]] bool is_terminal_status(std::string_view status) noexcept;

} // namespace tightrope::auth::api_keys
