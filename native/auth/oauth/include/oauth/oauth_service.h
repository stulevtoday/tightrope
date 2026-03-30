#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <memory>

#include <sqlite3.h>

#include "provider_client.h"

namespace tightrope::auth::oauth {

struct StartRequest {
    std::optional<std::string> force_method;
};

struct StartResponse {
    int status = 500;
    std::string code;
    std::string message;
    std::string method;
    std::optional<std::string> authorization_url;
    std::optional<std::string> callback_url;
    std::optional<std::string> verification_url;
    std::optional<std::string> user_code;
    std::optional<std::string> device_auth_id;
    std::optional<int> interval_seconds;
    std::optional<int> expires_in_seconds;
};

struct StatusResponse {
    int status = 500;
    std::string code;
    std::string message;
    std::string oauth_status;
    std::optional<std::string> error_message;
    bool listener_running = false;
    std::optional<std::string> callback_url;
    std::optional<std::string> authorization_url;
};

struct CompleteRequest {
    std::optional<std::string> device_auth_id;
    std::optional<std::string> user_code;
};

struct CompleteResponse {
    int status = 500;
    std::string code;
    std::string message;
    std::string oauth_status;
};

struct ManualCallbackResponse {
    int status = 500;
    std::string code;
    std::string message;
    std::string oauth_status;
    std::optional<std::string> error_message;
};

struct BrowserCallbackResponse {
    int status = 500;
    std::string content_type = "text/html; charset=utf-8";
    std::string body;
};

class OauthService final {
  public:
    static OauthService& instance();

    StartResponse start(const StartRequest& request, sqlite3* db = nullptr);
    StatusResponse status();
    StatusResponse stop();
    StartResponse restart(sqlite3* db = nullptr);
    CompleteResponse complete(const CompleteRequest& request, sqlite3* db = nullptr);
    ManualCallbackResponse manual_callback(std::string_view callback_url, sqlite3* db = nullptr);
    BrowserCallbackResponse browser_callback(
        std::string_view code,
        std::string_view state,
        std::string_view error,
        sqlite3* db = nullptr
    );

    void set_provider_client_for_testing(std::shared_ptr<ProviderClient> provider);
    void clear_provider_client_for_testing();
    void reset_for_testing();

  private:
    OauthService();
    ~OauthService();
    OauthService(const OauthService&) = delete;
    OauthService& operator=(const OauthService&) = delete;
};

void set_provider_client_for_testing(std::shared_ptr<ProviderClient> provider);
void clear_provider_client_for_testing();
void reset_oauth_state_for_testing();

} // namespace tightrope::auth::oauth
