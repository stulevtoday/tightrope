#include "password_auth.h"

#include <sodium.h>

namespace tightrope::auth::dashboard {

namespace {

bool sodium_ready() noexcept {
    return sodium_init() >= 0;
}

} // namespace

std::optional<std::string> hash_password(const std::string_view password) noexcept {
    if (password.empty() || !sodium_ready()) {
        return std::nullopt;
    }

    char hash[crypto_pwhash_STRBYTES] = {};
    const int rc = crypto_pwhash_str(
        hash,
        password.data(),
        static_cast<unsigned long long>(password.size()),
        crypto_pwhash_OPSLIMIT_INTERACTIVE,
        crypto_pwhash_MEMLIMIT_INTERACTIVE
    );
    if (rc != 0) {
        return std::nullopt;
    }
    return std::string(hash);
}

bool verify_password(const std::string_view password, const std::string_view password_hash) noexcept {
    if (password.empty() || password_hash.empty() || !sodium_ready()) {
        return false;
    }
    return crypto_pwhash_str_verify(
               std::string(password_hash).c_str(),
               password.data(),
               static_cast<unsigned long long>(password.size())
           ) == 0;
}

} // namespace tightrope::auth::dashboard
