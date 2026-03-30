#include "totp_auth.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <vector>

#include <mbedtls/md.h>
#include <sodium.h>

namespace tightrope::auth::dashboard {

namespace {

constexpr std::string_view kBase32Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
constexpr std::int64_t kTotpPeriodSeconds = 30;

bool sodium_ready() noexcept {
    return sodium_init() >= 0;
}

std::string base32_encode(const std::vector<unsigned char>& input) {
    if (input.empty()) {
        return {};
    }
    std::string output;
    output.reserve((input.size() * 8 + 4) / 5);

    std::uint32_t buffer = 0;
    int bits_left = 0;
    for (const auto byte : input) {
        buffer = (buffer << 8U) | byte;
        bits_left += 8;
        while (bits_left >= 5) {
            const auto index = static_cast<std::size_t>((buffer >> (bits_left - 5)) & 0x1FU);
            output.push_back(kBase32Alphabet[index]);
            bits_left -= 5;
        }
    }
    if (bits_left > 0) {
        const auto index = static_cast<std::size_t>((buffer << (5 - bits_left)) & 0x1FU);
        output.push_back(kBase32Alphabet[index]);
    }
    return output;
}

int base32_index(const char ch) {
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    if (upper >= 'A' && upper <= 'Z') {
        return upper - 'A';
    }
    if (upper >= '2' && upper <= '7') {
        return 26 + (upper - '2');
    }
    return -1;
}

std::optional<std::vector<unsigned char>> base32_decode(std::string_view value) {
    std::vector<unsigned char> output;
    if (value.empty()) {
        return output;
    }
    output.reserve((value.size() * 5) / 8);

    std::uint32_t buffer = 0;
    int bits_left = 0;
    for (const auto ch : value) {
        if (std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == '-') {
            continue;
        }
        const int idx = base32_index(ch);
        if (idx < 0) {
            return std::nullopt;
        }
        buffer = (buffer << 5U) | static_cast<std::uint32_t>(idx);
        bits_left += 5;
        while (bits_left >= 8) {
            const auto byte = static_cast<unsigned char>((buffer >> (bits_left - 8)) & 0xFFU);
            output.push_back(byte);
            bits_left -= 8;
        }
    }
    return output;
}

std::optional<std::string> code_for_step(const std::vector<unsigned char>& secret, const std::int64_t step) {
    if (secret.empty() || step < 0) {
        return std::nullopt;
    }
    std::array<unsigned char, 8> counter{};
    auto counter_value = static_cast<std::uint64_t>(step);
    for (int i = 7; i >= 0; --i) {
        counter[static_cast<std::size_t>(i)] = static_cast<unsigned char>(counter_value & 0xFFU);
        counter_value >>= 8U;
    }

    const auto* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (md_info == nullptr) {
        return std::nullopt;
    }

    std::array<unsigned char, 20> digest{};
    if (mbedtls_md_hmac(md_info, secret.data(), secret.size(), counter.data(), counter.size(), digest.data()) != 0) {
        return std::nullopt;
    }

    const int offset = digest.back() & 0x0F;
    const std::uint32_t binary = (static_cast<std::uint32_t>(digest[static_cast<std::size_t>(offset)]) & 0x7FU) << 24U |
                                 (static_cast<std::uint32_t>(digest[static_cast<std::size_t>(offset + 1)]) & 0xFFU)
                                     << 16U |
                                 (static_cast<std::uint32_t>(digest[static_cast<std::size_t>(offset + 2)]) & 0xFFU)
                                     << 8U |
                                 (static_cast<std::uint32_t>(digest[static_cast<std::size_t>(offset + 3)]) & 0xFFU);

    const std::uint32_t otp = binary % 1000000U;
    char buffer[7] = {};
    std::snprintf(buffer, sizeof(buffer), "%06u", otp);
    return std::string(buffer);
}

std::string percent_encode(std::string_view value) {
    std::string encoded;
    encoded.reserve(value.size() * 3);
    constexpr auto is_unreserved = [](const char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '-' || ch == '_' || ch == '.' || ch == '~';
    };
    for (const auto ch : value) {
        if (is_unreserved(ch)) {
            encoded.push_back(ch);
            continue;
        }
        char hex[4] = {};
        std::snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned char>(ch));
        encoded += hex;
    }
    return encoded;
}

} // namespace

std::string generate_totp_secret(const std::size_t bytes_length) {
    const std::size_t length = std::max<std::size_t>(1, bytes_length);
    if (!sodium_ready()) {
        return {};
    }
    std::vector<unsigned char> bytes(length);
    randombytes_buf(bytes.data(), bytes.size());
    return base32_encode(bytes);
}

std::optional<std::string> generate_totp_code(const std::string_view base32_secret, const std::int64_t unix_time) {
    const auto decoded = base32_decode(base32_secret);
    if (!decoded.has_value() || unix_time < 0) {
        return std::nullopt;
    }
    return code_for_step(*decoded, unix_time / kTotpPeriodSeconds);
}

bool verify_totp_code(
    const std::string_view base32_secret,
    const std::string_view code,
    const std::int64_t unix_time,
    const int window,
    const std::optional<std::int64_t>& last_verified_step,
    std::int64_t* matched_step_out
) noexcept {
    if (unix_time < 0 || code.size() != 6) {
        return false;
    }
    const auto decoded = base32_decode(base32_secret);
    if (!decoded.has_value() || decoded->empty()) {
        return false;
    }

    const auto current_step = unix_time / kTotpPeriodSeconds;
    const int safe_window = std::max(0, window);
    for (int offset = -safe_window; offset <= safe_window; ++offset) {
        const std::int64_t step = current_step + offset;
        if (step < 0) {
            continue;
        }
        if (last_verified_step.has_value() && step <= *last_verified_step) {
            continue;
        }
        const auto expected = code_for_step(*decoded, step);
        if (!expected.has_value()) {
            continue;
        }
        if (*expected != code) {
            continue;
        }
        if (matched_step_out != nullptr) {
            *matched_step_out = step;
        }
        return true;
    }
    return false;
}

std::string build_totp_otpauth_uri(
    const std::string_view base32_secret,
    const std::string_view issuer,
    const std::string_view account_name
) {
    const auto issuer_encoded = percent_encode(issuer);
    const auto account_encoded = percent_encode(account_name);
    return "otpauth://totp/" + issuer_encoded + ":" + account_encoded + "?secret=" + std::string(base32_secret) +
           "&issuer=" + issuer_encoded;
}

} // namespace tightrope::auth::dashboard
