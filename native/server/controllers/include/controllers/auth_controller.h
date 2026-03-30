#pragma once
// auth API controller

#include <string>
#include <string_view>

#include <sqlite3.h>

namespace tightrope::server::controllers {

struct DashboardAuthSessionPayload {
    bool authenticated = false;
    bool password_required = false;
    bool totp_required_on_login = false;
    bool totp_configured = false;
};

struct DashboardAuthResponse {
    int status = 500;
    std::string code;
    std::string message;
    DashboardAuthSessionPayload session;
    std::string session_id;
};

struct TotpSetupStartResponse {
    int status = 500;
    std::string code;
    std::string message;
    std::string secret;
    std::string otpauth_uri;
};

DashboardAuthResponse get_dashboard_auth_session(std::string_view session_id, sqlite3* db = nullptr);
DashboardAuthResponse setup_dashboard_password(std::string_view password, sqlite3* db = nullptr);
DashboardAuthResponse login_dashboard_password(std::string_view password, sqlite3* db = nullptr);
TotpSetupStartResponse start_totp_setup(std::string_view session_id, sqlite3* db = nullptr);
DashboardAuthResponse
confirm_totp_setup(std::string_view session_id, std::string_view secret, std::string_view code, sqlite3* db = nullptr);
DashboardAuthResponse verify_dashboard_totp(std::string_view session_id, std::string_view code, sqlite3* db = nullptr);
void logout_dashboard(std::string_view session_id);

} // namespace tightrope::server::controllers
