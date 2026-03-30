#pragma once
// TOTP verification

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tightrope::auth::dashboard {

[[nodiscard]] std::string generate_totp_secret(std::size_t bytes_length = 20);
[[nodiscard]] std::optional<std::string> generate_totp_code(std::string_view base32_secret, std::int64_t unix_time);
[[nodiscard]] bool verify_totp_code(
    std::string_view base32_secret,
    std::string_view code,
    std::int64_t unix_time,
    int window,
    const std::optional<std::int64_t>& last_verified_step = std::nullopt,
    std::int64_t* matched_step_out = nullptr
) noexcept;
[[nodiscard]] std::string build_totp_otpauth_uri(
    std::string_view base32_secret,
    std::string_view issuer,
    std::string_view account_name
);

} // namespace tightrope::auth::dashboard
