#pragma once
// oauth API controller

#include <optional>
#include <string>
#include <string_view>

#include <sqlite3.h>

namespace tightrope::server::controllers {

struct OauthStartRequest {
    std::optional<std::string> force_method;
};

struct OauthStartResponse {
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

struct OauthStatusResponse {
    int status = 500;
    std::string code;
    std::string message;
    std::string oauth_status;
    std::optional<std::string> error_message;
    bool listener_running = false;
    std::optional<std::string> callback_url;
    std::optional<std::string> authorization_url;
};

struct OauthCompleteRequest {
    std::optional<std::string> device_auth_id;
    std::optional<std::string> user_code;
};

struct OauthCompleteResponse {
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

OauthStartResponse start_oauth(const OauthStartRequest& request, sqlite3* db = nullptr);
OauthStatusResponse oauth_status();
OauthStatusResponse stop_oauth();
OauthStartResponse restart_oauth(sqlite3* db = nullptr);
OauthCompleteResponse complete_oauth(const OauthCompleteRequest& request);
ManualCallbackResponse manual_oauth_callback(std::string_view callback_url);
BrowserCallbackResponse browser_oauth_callback(
    std::string_view code,
    std::string_view state,
    std::string_view error
);

} // namespace tightrope::server::controllers
