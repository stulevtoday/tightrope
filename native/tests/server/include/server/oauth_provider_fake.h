#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include <mbedtls/base64.h>

#include "oauth/provider_client.h"

namespace tightrope::tests::server {

inline std::string base64_url_encode(const std::string_view input) {
    std::size_t encoded_len = 0;
    (void)mbedtls_base64_encode(
        nullptr,
        0,
        &encoded_len,
        reinterpret_cast<const unsigned char*>(input.data()),
        input.size()
    );
    std::string encoded(encoded_len, '\0');
    if (mbedtls_base64_encode(
            reinterpret_cast<unsigned char*>(encoded.data()),
            encoded.size(),
            &encoded_len,
            reinterpret_cast<const unsigned char*>(input.data()),
            input.size()
        ) != 0) {
        return {};
    }
    encoded.resize(encoded_len);
    for (char& ch : encoded) {
        if (ch == '+') {
            ch = '-';
        } else if (ch == '/') {
            ch = '_';
        }
    }
    while (!encoded.empty() && encoded.back() == '=') {
        encoded.pop_back();
    }
    return encoded;
}

inline std::string make_id_token(const std::string_view email) {
    const std::string header = R"({"alg":"none","typ":"JWT"})";
    const std::string payload = std::string(R"({"email":")") + std::string(email) +
                                R"(","chatgpt_account_id":"acct_test","chatgpt_plan_type":"plus"})";
    return base64_url_encode(header) + "." + base64_url_encode(payload) + ".signature";
}

class OAuthProviderFake final : public auth::oauth::ProviderClient {
  public:
    explicit OAuthProviderFake(std::string email, int pending_device_polls = 2)
        : email_(std::move(email)),
          pending_device_polls_(std::max(0, pending_device_polls)) {}

    auth::oauth::ProviderResult<auth::oauth::DeviceCodePayload> request_device_code() override {
        ++device_code_calls_;
        return auth::oauth::ProviderResult<auth::oauth::DeviceCodePayload>::ok({
            .verification_url = "https://auth.openai.com/codex/device",
            .user_code = "ABCD-EFGH",
            .device_auth_id = "dev_test_1234",
            .interval_seconds = 0,
            .expires_in_seconds = 60,
        });
    }

    auth::oauth::ProviderResult<auth::oauth::OAuthTokens>
    exchange_authorization_code(const auth::oauth::AuthorizationCodeRequest&) override {
        ++authorization_exchange_calls_;
        return auth::oauth::ProviderResult<auth::oauth::OAuthTokens>::ok({
            .access_token = "access-token",
            .refresh_token = "refresh-token",
            .id_token = make_id_token(email_),
        });
    }

    auth::oauth::ProviderResult<auth::oauth::OAuthTokens> refresh_access_token(std::string_view refresh_token) override {
        ++refresh_exchange_calls_;
        if (refresh_token.empty()) {
            return auth::oauth::ProviderResult<auth::oauth::OAuthTokens>::fail({
                .code = "invalid_refresh_token",
                .message = "Refresh token is missing",
                .status_code = 0,
            });
        }
        return auth::oauth::ProviderResult<auth::oauth::OAuthTokens>::ok({
            .access_token = "access-token-refreshed",
            .refresh_token = std::string(refresh_token),
            .id_token = make_id_token(email_),
        });
    }

    auth::oauth::DeviceTokenPollResult
    exchange_device_token(const auth::oauth::DeviceTokenPollRequest& request) override {
        ++device_poll_calls_;
        last_device_auth_id_ = request.device_auth_id;
        last_user_code_ = request.user_code;
        if (device_poll_calls_ <= pending_device_polls_) {
            return {.kind = auth::oauth::DeviceTokenPollKind::pending};
        }
        return {
            .kind = auth::oauth::DeviceTokenPollKind::authorization_code,
            .authorization_code = "device-auth-code",
            .code_verifier = "device-code-verifier",
        };
    }

    [[nodiscard]] int authorization_exchange_calls() const {
        return authorization_exchange_calls_;
    }

    [[nodiscard]] int device_poll_calls() const {
        return device_poll_calls_;
    }

    [[nodiscard]] int refresh_exchange_calls() const {
        return refresh_exchange_calls_;
    }

  private:
    std::string email_;
    int pending_device_polls_ = 0;
    int device_code_calls_ = 0;
    int authorization_exchange_calls_ = 0;
    int refresh_exchange_calls_ = 0;
    int device_poll_calls_ = 0;
    std::string last_device_auth_id_;
    std::string last_user_code_;
};

} // namespace tightrope::tests::server
