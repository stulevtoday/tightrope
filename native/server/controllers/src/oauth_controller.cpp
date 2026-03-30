#include "oauth_controller.h"

#include "controller_db.h"
#include "oauth/oauth_service.h"

namespace tightrope::server::controllers {

namespace {

auth::oauth::StartRequest to_service_request(const OauthStartRequest& request) {
    auth::oauth::StartRequest service_request;
    service_request.force_method = request.force_method;
    return service_request;
}

OauthStartResponse from_service_response(const auth::oauth::StartResponse& response) {
    return {
        .status = response.status,
        .code = response.code,
        .message = response.message,
        .method = response.method,
        .authorization_url = response.authorization_url,
        .callback_url = response.callback_url,
        .verification_url = response.verification_url,
        .user_code = response.user_code,
        .device_auth_id = response.device_auth_id,
        .interval_seconds = response.interval_seconds,
        .expires_in_seconds = response.expires_in_seconds,
    };
}

OauthStatusResponse from_service_response(const auth::oauth::StatusResponse& response) {
    return {
        .status = response.status,
        .code = response.code,
        .message = response.message,
        .oauth_status = response.oauth_status,
        .error_message = response.error_message,
        .listener_running = response.listener_running,
        .callback_url = response.callback_url,
        .authorization_url = response.authorization_url,
    };
}

auth::oauth::CompleteRequest to_service_request(const OauthCompleteRequest& request) {
    return {
        .device_auth_id = request.device_auth_id,
        .user_code = request.user_code,
    };
}

OauthCompleteResponse from_service_response(const auth::oauth::CompleteResponse& response) {
    return {
        .status = response.status,
        .code = response.code,
        .message = response.message,
        .oauth_status = response.oauth_status,
    };
}

ManualCallbackResponse from_service_response(const auth::oauth::ManualCallbackResponse& response) {
    return {
        .status = response.status,
        .code = response.code,
        .message = response.message,
        .oauth_status = response.oauth_status,
        .error_message = response.error_message,
    };
}

BrowserCallbackResponse from_service_response(const auth::oauth::BrowserCallbackResponse& response) {
    return {
        .status = response.status,
        .content_type = response.content_type,
        .body = response.body,
    };
}

} // namespace

OauthStartResponse start_oauth(const OauthStartRequest& request, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }
    return from_service_response(auth::oauth::OauthService::instance().start(to_service_request(request), handle.db));
}

OauthStatusResponse oauth_status() {
    return from_service_response(auth::oauth::OauthService::instance().status());
}

OauthStatusResponse stop_oauth() {
    return from_service_response(auth::oauth::OauthService::instance().stop());
}

OauthStartResponse restart_oauth(sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }
    return from_service_response(auth::oauth::OauthService::instance().restart(handle.db));
}

OauthCompleteResponse complete_oauth(const OauthCompleteRequest& request) {
    return from_service_response(auth::oauth::OauthService::instance().complete(to_service_request(request), nullptr));
}

ManualCallbackResponse manual_oauth_callback(const std::string_view callback_url) {
    auto handle = open_controller_db(nullptr);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
            .oauth_status = "error",
            .error_message = "Database unavailable",
        };
    }
    return from_service_response(auth::oauth::OauthService::instance().manual_callback(callback_url, handle.db));
}

BrowserCallbackResponse browser_oauth_callback(
    const std::string_view code,
    const std::string_view state,
    const std::string_view error
) {
    auto handle = open_controller_db(nullptr);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .body = "<!doctype html><html><body><h1>Database unavailable.</h1></body></html>",
        };
    }
    return from_service_response(auth::oauth::OauthService::instance().browser_callback(code, state, error, handle.db));
}

} // namespace tightrope::server::controllers
