#include "key_validator.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>

#include <mbedtls/sha256.h>
#include <sodium.h>

namespace tightrope::auth::api_keys {

namespace {

constexpr std::string_view kApiKeyPrefix = "sk-clb-";

bool sodium_ready() noexcept {
    return sodium_init() >= 0;
}

std::string bytes_to_hex(const unsigned char* bytes, const std::size_t size) {
    if (bytes == nullptr || size == 0) {
        return {};
    }
    std::string output(size * 2, '\0');
    for (std::size_t i = 0; i < size; ++i) {
        std::snprintf(output.data() + (i * 2), 3, "%02x", static_cast<unsigned int>(bytes[i]));
    }
    return output;
}

} // namespace

std::optional<std::string> hash_key(const std::string_view plain_key) noexcept {
    if (plain_key.empty()) {
        return std::nullopt;
    }

    std::array<unsigned char, 32> digest{};
    const int rc = mbedtls_sha256(
        reinterpret_cast<const unsigned char*>(plain_key.data()),
        plain_key.size(),
        digest.data(),
        0
    );
    if (rc != 0) {
        return std::nullopt;
    }
    return bytes_to_hex(digest.data(), digest.size());
}

bool verify_key_hash(const std::string_view plain_key, const std::string_view expected_hash) noexcept {
    const auto computed = hash_key(plain_key);
    return computed.has_value() && *computed == expected_hash;
}

std::optional<IssuedApiKeyMaterial> issue_api_key_material() noexcept {
    if (!sodium_ready()) {
        return std::nullopt;
    }

    std::array<unsigned char, 32> random_bytes{};
    randombytes_buf(random_bytes.data(), random_bytes.size());

    constexpr auto kVariant = sodium_base64_VARIANT_URLSAFE_NO_PADDING;
    const auto encoded_size = sodium_base64_encoded_len(random_bytes.size(), kVariant);
    std::string encoded(encoded_size, '\0');
    sodium_bin2base64(
        encoded.data(),
        encoded_size,
        random_bytes.data(),
        random_bytes.size(),
        kVariant
    );
    if (!encoded.empty() && encoded.back() == '\0') {
        encoded.pop_back();
    }

    IssuedApiKeyMaterial material;
    material.plain_key = std::string(kApiKeyPrefix) + encoded;
    material.key_prefix = material.plain_key.substr(0, std::min<std::size_t>(15, material.plain_key.size()));
    auto hash = hash_key(material.plain_key);
    if (!hash.has_value()) {
        return std::nullopt;
    }
    material.key_hash = *hash;
    return material;
}

} // namespace tightrope::auth::api_keys
