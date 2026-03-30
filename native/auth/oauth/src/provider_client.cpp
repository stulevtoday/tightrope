#include "provider_client.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>

#include <curl/curl.h>
#include <glaze/glaze.hpp>

#include "text/json_escape.h"

namespace tightrope::auth::oauth {

namespace {

using Json = glz::generic;

struct HttpResponse {
    long status_code = 0;
    std::string body;
};

std::string env_or_default(const char* key, std::string fallback) {
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') {
        return fallback;
    }
    return raw;
}

double env_or_default_double(const char* key, const double fallback) {
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') {
        return fallback;
    }
    char* end = nullptr;
    const double parsed = std::strtod(raw, &end);
    if (end == raw || !std::isfinite(parsed) || parsed <= 0.0) {
        return fallback;
    }
    return parsed;
}

std::string trim_trailing_slashes(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

std::optional<Json::object_t> parse_json_object(const std::string_view body) {
    Json payload;
    if (const auto ec = glz::read_json(payload, body); ec || !payload.is_object()) {
        return std::nullopt;
    }
    return payload.get_object();
}

std::optional<std::string> json_string(const Json::object_t& payload, const std::string_view key) {
    const auto it = payload.find(std::string(key));
    if (it == payload.end() || !it->second.is_string()) {
        return std::nullopt;
    }
    return it->second.get_string();
}

std::optional<int> json_int(const Json::object_t& payload, const std::string_view key) {
    const auto it = payload.find(std::string(key));
    if (it == payload.end() || !it->second.is_number()) {
        return std::nullopt;
    }
    return static_cast<int>(it->second.get_number());
}

std::optional<std::string> extract_error_code(const std::optional<Json::object_t>& payload) {
    if (!payload.has_value()) {
        return std::nullopt;
    }
    if (const auto error = json_string(*payload, "error"); error.has_value()) {
        return error;
    }
    if (const auto code = json_string(*payload, "error_code"); code.has_value()) {
        return code;
    }
    if (const auto code = json_string(*payload, "code"); code.has_value()) {
        return code;
    }
    const auto it = payload->find("error");
    if (it != payload->end() && it->second.is_object()) {
        const auto& error_object = it->second.get_object();
        if (const auto code = json_string(error_object, "code"); code.has_value()) {
            return code;
        }
        if (const auto nested = json_string(error_object, "error"); nested.has_value()) {
            return nested;
        }
    }
    return std::nullopt;
}

std::optional<std::string> extract_error_message(const std::optional<Json::object_t>& payload) {
    if (!payload.has_value()) {
        return std::nullopt;
    }
    if (const auto message = json_string(*payload, "message"); message.has_value()) {
        return message;
    }
    if (const auto desc = json_string(*payload, "error_description"); desc.has_value()) {
        return desc;
    }
    const auto it = payload->find("error");
    if (it != payload->end()) {
        if (it->second.is_string()) {
            return it->second.get_string();
        }
        if (it->second.is_object()) {
            const auto& error_object = it->second.get_object();
            if (const auto message = json_string(error_object, "message"); message.has_value()) {
                return message;
            }
            if (const auto desc = json_string(error_object, "error_description"); desc.has_value()) {
                return desc;
            }
        }
    }
    return std::nullopt;
}

OAuthError oauth_error_from_payload(
    const long status_code,
    const std::optional<Json::object_t>& payload,
    const std::string_view fallback_prefix
) {
    OAuthError error;
    error.status_code = static_cast<int>(status_code);
    if (const auto code = extract_error_code(payload); code.has_value()) {
        error.code = *code;
    } else {
        error.code = "http_" + std::to_string(status_code);
    }
    if (const auto message = extract_error_message(payload); message.has_value()) {
        error.message = *message;
    } else {
        error.message = std::string(fallback_prefix) + " (" + std::to_string(status_code) + ")";
    }
    return error;
}

bool is_pending_error(const std::optional<Json::object_t>& payload) {
    const auto code = extract_error_code(payload);
    if (code.has_value() && (*code == "authorization_pending" || *code == "slow_down")) {
        return true;
    }
    if (!payload.has_value()) {
        return false;
    }
    if (const auto status = json_string(*payload, "status"); status.has_value()) {
        std::string normalized = *status;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (normalized == "pending" || normalized == "authorization_pending") {
            return true;
        }
    }
    return false;
}

size_t write_callback(char* ptr, const size_t size, const size_t nmemb, void* userdata) {
    const auto* chunk = ptr;
    auto* output = static_cast<std::string*>(userdata);
    output->append(chunk, size * nmemb);
    return size * nmemb;
}

ProviderResult<HttpResponse> perform_post_request(
    const std::string& url,
    const std::string& body,
    const std::string& content_type
) {
    static std::once_flag curl_once;
    std::call_once(curl_once, [] {
        (void)curl_global_init(CURL_GLOBAL_DEFAULT);
    });

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return ProviderResult<HttpResponse>::fail({
            .code = "oauth_transport_init_failed",
            .message = "Failed to initialize OAuth HTTP client",
            .status_code = 0,
        });
    }

    HttpResponse response;
    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Content-Type: " + content_type).c_str());
    headers = curl_slist_append(headers, "Accept: application/json");

    const long timeout_ms = static_cast<long>(oauth_timeout_seconds() * 1000.0);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "tightrope-native/0.1");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    const auto result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
        const std::string message = curl_easy_strerror(result);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return ProviderResult<HttpResponse>::fail({
            .code = "oauth_transport_failed",
            .message = "OAuth HTTP request failed: " + message,
            .status_code = 0,
        });
    }

    (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return ProviderResult<HttpResponse>::ok(std::move(response));
}

std::string form_url_encode(CURL* curl, const std::string_view value) {
    if (value.empty()) {
        return {};
    }
    char* escaped = curl_easy_escape(curl, value.data(), static_cast<int>(value.size()));
    if (escaped == nullptr) {
        return {};
    }
    const std::string output(escaped);
    curl_free(escaped);
    return output;
}

class CurlProviderClient final : public ProviderClient {
  public:
    ProviderResult<DeviceCodePayload> request_device_code() override {
        const std::string url = oauth_auth_base_url() + "/api/accounts/deviceauth/usercode";
        const std::string body = std::string(R"({"client_id":)") + core::text::quote_json_string(oauth_client_id()) + "}";
        const auto response = perform_post_request(url, body, "application/json");
        if (!response.is_ok()) {
            return ProviderResult<DeviceCodePayload>::fail(*response.error);
        }

        const auto payload = parse_json_object(response.value->body);
        if (response.value->status_code == 404) {
            return ProviderResult<DeviceCodePayload>::fail({
                .code = "device_auth_unavailable",
                .message = "Device code login is not enabled for this server. Use browser login or verify the server URL.",
                .status_code = 404,
            });
        }
        if (response.value->status_code >= 400) {
            return ProviderResult<DeviceCodePayload>::fail(oauth_error_from_payload(
                response.value->status_code,
                payload,
                "Device code request failed"
            ));
        }

        const auto user_code = payload.has_value() ? json_string(*payload, "user_code") : std::nullopt;
        const auto device_auth_id = payload.has_value() ? json_string(*payload, "device_auth_id") : std::nullopt;
        if (!user_code.has_value() || !device_auth_id.has_value() || user_code->empty() || device_auth_id->empty()) {
            return ProviderResult<DeviceCodePayload>::fail({
                .code = "invalid_response",
                .message = "Device auth response missing fields",
                .status_code = static_cast<int>(response.value->status_code),
            });
        }
        const int interval_seconds = payload.has_value() ? json_int(*payload, "interval").value_or(0) : 0;
        const int expires_in_seconds = payload.has_value() ? json_int(*payload, "expires_in").value_or(900) : 900;

        return ProviderResult<DeviceCodePayload>::ok({
            .verification_url = oauth_auth_base_url() + "/codex/device",
            .user_code = *user_code,
            .device_auth_id = *device_auth_id,
            .interval_seconds = interval_seconds,
            .expires_in_seconds = expires_in_seconds <= 0 ? 900 : expires_in_seconds,
        });
    }

    ProviderResult<OAuthTokens> exchange_authorization_code(const AuthorizationCodeRequest& request) override {
        const std::string url = oauth_auth_base_url() + "/oauth/token";
        CURL* curl = curl_easy_init();
        if (curl == nullptr) {
            return ProviderResult<OAuthTokens>::fail({
                .code = "oauth_transport_init_failed",
                .message = "Failed to initialize OAuth HTTP client",
                .status_code = 0,
            });
        }
        const std::string redirect_uri = request.redirect_uri.value_or(oauth_redirect_uri());
        const std::string form_body = "grant_type=authorization_code&client_id=" + form_url_encode(curl, oauth_client_id()) +
                                      "&code=" + form_url_encode(curl, request.code) + "&code_verifier=" +
                                      form_url_encode(curl, request.code_verifier) + "&redirect_uri=" +
                                      form_url_encode(curl, redirect_uri);
        curl_easy_cleanup(curl);

        const auto response = perform_post_request(url, form_body, "application/x-www-form-urlencoded");
        if (!response.is_ok()) {
            return ProviderResult<OAuthTokens>::fail(*response.error);
        }
        return parse_token_response(response.value->status_code, response.value->body);
    }

    ProviderResult<OAuthTokens> refresh_access_token(const std::string_view refresh_token) override {
        if (refresh_token.empty()) {
            return ProviderResult<OAuthTokens>::fail({
                .code = "invalid_refresh_token",
                .message = "Refresh token is missing",
                .status_code = 0,
            });
        }

        const std::string url = oauth_auth_base_url() + "/oauth/token";
        CURL* curl = curl_easy_init();
        if (curl == nullptr) {
            return ProviderResult<OAuthTokens>::fail({
                .code = "oauth_transport_init_failed",
                .message = "Failed to initialize OAuth HTTP client",
                .status_code = 0,
            });
        }
        const std::string form_body = "grant_type=refresh_token&client_id=" + form_url_encode(curl, oauth_client_id()) +
                                      "&refresh_token=" + form_url_encode(curl, refresh_token);
        curl_easy_cleanup(curl);

        const auto response = perform_post_request(url, form_body, "application/x-www-form-urlencoded");
        if (!response.is_ok()) {
            return ProviderResult<OAuthTokens>::fail(*response.error);
        }

        const auto payload = parse_json_object(response.value->body);
        if (response.value->status_code >= 400) {
            return ProviderResult<OAuthTokens>::fail(oauth_error_from_payload(
                response.value->status_code,
                payload,
                "OAuth refresh failed"
            ));
        }
        if (!payload.has_value()) {
            return ProviderResult<OAuthTokens>::fail({
                .code = "invalid_response",
                .message = "OAuth refresh response invalid",
                .status_code = static_cast<int>(response.value->status_code),
            });
        }

        const auto access_token = json_string(*payload, "access_token");
        if (!access_token.has_value() || access_token->empty()) {
            return ProviderResult<OAuthTokens>::fail({
                .code = "invalid_response",
                .message = "OAuth refresh response missing access token",
                .status_code = static_cast<int>(response.value->status_code),
            });
        }

        auto next_refresh_token = json_string(*payload, "refresh_token").value_or(std::string(refresh_token));
        if (next_refresh_token.empty()) {
            next_refresh_token = std::string(refresh_token);
        }
        auto next_id_token = json_string(*payload, "id_token").value_or(std::string{});

        return ProviderResult<OAuthTokens>::ok({
            .access_token = *access_token,
            .refresh_token = std::move(next_refresh_token),
            .id_token = std::move(next_id_token),
        });
    }

    DeviceTokenPollResult exchange_device_token(const DeviceTokenPollRequest& request) override {
        const std::string url = oauth_auth_base_url() + "/api/accounts/deviceauth/token";
        const std::string body = std::string(R"({"device_auth_id":)") + core::text::quote_json_string(request.device_auth_id) +
                                 R"(,"user_code":)" + core::text::quote_json_string(request.user_code) + "}";
        const auto response = perform_post_request(url, body, "application/json");
        if (!response.is_ok()) {
            return {
                .kind = DeviceTokenPollKind::error,
                .error = *response.error,
            };
        }

        const auto payload = parse_json_object(response.value->body);
        if (response.value->status_code == 403 || response.value->status_code == 404) {
            return {.kind = DeviceTokenPollKind::pending};
        }
        if (response.value->status_code >= 400) {
            if (is_pending_error(payload)) {
                return {.kind = DeviceTokenPollKind::pending};
            }
            return {
                .kind = DeviceTokenPollKind::error,
                .error = oauth_error_from_payload(response.value->status_code, payload, "Device token request failed"),
            };
        }
        if (is_pending_error(payload)) {
            return {.kind = DeviceTokenPollKind::pending};
        }

        if (payload.has_value()) {
            const auto authorization_code = json_string(*payload, "authorization_code");
            if (authorization_code.has_value() && !authorization_code->empty()) {
                const auto code_verifier = json_string(*payload, "code_verifier");
                if (!code_verifier.has_value() || code_verifier->empty()) {
                    return {
                        .kind = DeviceTokenPollKind::error,
                        .error = OAuthError{
                            .code = "invalid_response",
                            .message = "Device auth response missing code verifier",
                            .status_code = static_cast<int>(response.value->status_code),
                        },
                    };
                }
                return {
                    .kind = DeviceTokenPollKind::authorization_code,
                    .authorization_code = *authorization_code,
                    .code_verifier = *code_verifier,
                };
            }
        }

        const auto token_response = parse_token_response(response.value->status_code, response.value->body);
        if (token_response.is_ok()) {
            return {
                .kind = DeviceTokenPollKind::tokens,
                .tokens = *token_response.value,
            };
        }
        return {
            .kind = DeviceTokenPollKind::error,
            .error = token_response.error.value_or(OAuthError{
                .code = "invalid_response",
                .message = "Device auth response invalid",
                .status_code = static_cast<int>(response.value->status_code),
            }),
        };
    }
};

} // namespace

ProviderResult<OAuthTokens> parse_token_response(const long status_code, const std::string_view body) {
    const auto payload = parse_json_object(body);
    if (status_code >= 400) {
        return ProviderResult<OAuthTokens>::fail(oauth_error_from_payload(status_code, payload, "OAuth request failed"));
    }
    if (!payload.has_value()) {
        return ProviderResult<OAuthTokens>::fail({
            .code = "invalid_response",
            .message = "OAuth response invalid",
            .status_code = static_cast<int>(status_code),
        });
    }

    const auto access_token = json_string(*payload, "access_token");
    const auto refresh_token = json_string(*payload, "refresh_token");
    const auto id_token = json_string(*payload, "id_token");
    if (!access_token.has_value() || !refresh_token.has_value() || !id_token.has_value() || access_token->empty() ||
        refresh_token->empty() || id_token->empty()) {
        return ProviderResult<OAuthTokens>::fail({
            .code = "invalid_response",
            .message = "OAuth response missing tokens",
            .status_code = static_cast<int>(status_code),
        });
    }
    return ProviderResult<OAuthTokens>::ok({
        .access_token = *access_token,
        .refresh_token = *refresh_token,
        .id_token = *id_token,
    });
}

std::string ensure_offline_access(std::string scope) {
    std::string token;
    std::string copy = scope;
    std::size_t cursor = 0;
    while (cursor < copy.size()) {
        while (cursor < copy.size() && std::isspace(static_cast<unsigned char>(copy[cursor])) != 0) {
            ++cursor;
        }
        std::size_t next = cursor;
        while (next < copy.size() && std::isspace(static_cast<unsigned char>(copy[next])) == 0) {
            ++next;
        }
        if (next > cursor) {
            token = copy.substr(cursor, next - cursor);
            if (token == "offline_access") {
                return scope;
            }
        }
        cursor = next;
    }
    if (scope.empty()) {
        return "offline_access";
    }
    return scope + " offline_access";
}

std::string oauth_auth_base_url() {
    return trim_trailing_slashes(env_or_default("TIGHTROPE_AUTH_BASE_URL", "https://auth.openai.com"));
}

std::string oauth_client_id() {
    return env_or_default("TIGHTROPE_OAUTH_CLIENT_ID", "app_EMoamEEZ73f0CkXaXp7hrann");
}

std::string oauth_redirect_uri() {
    return env_or_default("TIGHTROPE_OAUTH_REDIRECT_URI", "http://localhost:1455/auth/callback");
}

std::string oauth_scope() {
    return ensure_offline_access(env_or_default("TIGHTROPE_OAUTH_SCOPE", "openid profile email"));
}

std::string oauth_originator() {
    return env_or_default("TIGHTROPE_OAUTH_ORIGINATOR", "codex_chatgpt_desktop");
}

double oauth_timeout_seconds() {
    return env_or_default_double("TIGHTROPE_OAUTH_TIMEOUT_SECONDS", 30.0);
}

std::shared_ptr<ProviderClient> make_default_provider_client() {
    return std::make_shared<CurlProviderClient>();
}

} // namespace tightrope::auth::oauth
