#pragma once
// sk-clb-... key validation + hashing

#include <optional>
#include <string>
#include <string_view>

namespace tightrope::auth::api_keys {

struct IssuedApiKeyMaterial {
    std::string plain_key;
    std::string key_hash;
    std::string key_prefix;
};

[[nodiscard]] std::optional<IssuedApiKeyMaterial> issue_api_key_material() noexcept;
[[nodiscard]] std::optional<std::string> hash_key(std::string_view plain_key) noexcept;
[[nodiscard]] bool verify_key_hash(std::string_view plain_key, std::string_view expected_hash) noexcept;

} // namespace tightrope::auth::api_keys
