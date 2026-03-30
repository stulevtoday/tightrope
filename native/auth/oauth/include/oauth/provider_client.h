#pragma once

#include <optional>
#include <memory>
#include <string>
#include <utility>

namespace tightrope::auth::oauth {

struct OAuthError {
    std::string code;
    std::string message;
    int status_code = 0;
};

template <typename T>
struct ProviderResult {
    std::optional<T> value;
    std::optional<OAuthError> error;

    [[nodiscard]] static ProviderResult ok(T next_value) {
        return {
            .value = std::move(next_value),
            .error = std::nullopt,
        };
    }

    [[nodiscard]] static ProviderResult fail(OAuthError next_error) {
        return {
            .value = std::nullopt,
            .error = std::move(next_error),
        };
    }

    [[nodiscard]] bool is_ok() const {
        return value.has_value() && !error.has_value();
    }
};

struct OAuthTokens {
    std::string access_token;
    std::string refresh_token;
    std::string id_token;
};

struct DeviceCodePayload {
    std::string verification_url;
    std::string user_code;
    std::string device_auth_id;
    int interval_seconds = 0;
    int expires_in_seconds = 0;
};

struct AuthorizationCodeRequest {
    std::string code;
    std::string code_verifier;
    std::optional<std::string> redirect_uri;
};

struct DeviceTokenPollRequest {
    std::string device_auth_id;
    std::string user_code;
};

enum class DeviceTokenPollKind {
    pending,
    tokens,
    authorization_code,
    error,
};

struct DeviceTokenPollResult {
    DeviceTokenPollKind kind = DeviceTokenPollKind::pending;
    std::optional<OAuthTokens> tokens;
    std::optional<std::string> authorization_code;
    std::optional<std::string> code_verifier;
    std::optional<OAuthError> error;
};

class ProviderClient {
  public:
    virtual ~ProviderClient() = default;

    virtual ProviderResult<DeviceCodePayload> request_device_code() = 0;
    virtual ProviderResult<OAuthTokens> exchange_authorization_code(const AuthorizationCodeRequest& request) = 0;
    virtual ProviderResult<OAuthTokens> refresh_access_token(std::string_view refresh_token) = 0;
    virtual DeviceTokenPollResult exchange_device_token(const DeviceTokenPollRequest& request) = 0;
};

ProviderResult<OAuthTokens> parse_token_response(long status_code, std::string_view body);
std::string ensure_offline_access(std::string scope);
std::string oauth_auth_base_url();
std::string oauth_client_id();
std::string oauth_redirect_uri();
std::string oauth_scope();
std::string oauth_originator();
double oauth_timeout_seconds();
std::shared_ptr<ProviderClient> make_default_provider_client();

} // namespace tightrope::auth::oauth
