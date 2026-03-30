#include "auth_controller.h"

#include <chrono>
#include <unordered_map>

#include "dashboard/password_auth.h"
#include "dashboard/session_manager.h"
#include "dashboard/totp_auth.h"
#include "controller_db.h"
#include "text/ascii.h"
#include "repositories/settings_repo.h"

namespace tightrope::server::controllers {

namespace {

std::int64_t now_ms() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::int64_t now_unix_seconds() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(now).count();
}

auth::dashboard::DashboardSessionManager& session_manager() {
    static auth::dashboard::DashboardSessionManager manager;
    return manager;
}

std::unordered_map<std::string, std::string>& pending_totp_secret_by_session() {
    static std::unordered_map<std::string, std::string> pending;
    return pending;
}

DashboardAuthSessionPayload build_session_payload(
    const db::DashboardSettingsRecord& settings,
    const std::optional<auth::dashboard::DashboardSessionState>& session
) {
    const bool password_required = settings.password_hash.has_value() && !settings.password_hash->empty();
    const bool totp_configured = settings.totp_secret.has_value() && !settings.totp_secret->empty();
    const bool has_password_session = session.has_value() && session->password_verified;
    const bool has_totp_session = has_password_session && session->totp_verified;
    const bool totp_required_on_login = password_required && settings.totp_required_on_login && has_password_session;

    bool authenticated = false;
    if (!password_required) {
        authenticated = true;
    } else if (settings.totp_required_on_login) {
        authenticated = has_totp_session;
    } else {
        authenticated = has_password_session;
    }

    return {
        .authenticated = authenticated,
        .password_required = password_required,
        .totp_required_on_login = totp_required_on_login,
        .totp_configured = totp_configured,
    };
}

DashboardAuthResponse db_unavailable() {
    return {
        .status = 500,
        .code = "db_unavailable",
        .message = "Database unavailable",
    };
}

DashboardAuthResponse settings_unavailable() {
    return {
        .status = 500,
        .code = "settings_unavailable",
        .message = "Failed to load dashboard auth settings",
    };
}

DashboardAuthResponse auth_required() {
    return {
        .status = 401,
        .code = "auth_required",
        .message = "Authentication is required",
    };
}

} // namespace

DashboardAuthResponse get_dashboard_auth_session(const std::string_view session_id, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return db_unavailable();
    }

    const auto settings = db::get_dashboard_settings(handle.db);
    if (!settings.has_value()) {
        return settings_unavailable();
    }

    auto session = session_manager().get(session_id, now_ms());
    return {
        .status = 200,
        .session = build_session_payload(*settings, session),
    };
}

DashboardAuthResponse setup_dashboard_password(const std::string_view password, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return db_unavailable();
    }

    const auto settings = db::get_dashboard_settings(handle.db);
    if (!settings.has_value()) {
        return settings_unavailable();
    }
    if (settings->password_hash.has_value() && !settings->password_hash->empty()) {
        return {
            .status = 409,
            .code = "password_already_configured",
            .message = "Password is already configured",
        };
    }

    const auto trimmed = core::text::trim_ascii(password);
    if (trimmed.size() < 8) {
        return {
            .status = 400,
            .code = "invalid_password",
            .message = "Password must be at least 8 characters",
        };
    }

    const auto hash = auth::dashboard::hash_password(trimmed);
    if (!hash.has_value()) {
        return {
            .status = 500,
            .code = "password_hash_failed",
            .message = "Failed to hash password",
        };
    }

    if (!db::set_dashboard_password_hash(handle.db, *hash)) {
        return {
            .status = 500,
            .code = "password_store_failed",
            .message = "Failed to persist password hash",
        };
    }

    const auto session_id = session_manager().create(/*password_verified=*/true, /*totp_verified=*/false, now_ms());
    auto response = get_dashboard_auth_session(session_id, handle.db);
    response.session_id = session_id;
    return response;
}

DashboardAuthResponse login_dashboard_password(const std::string_view password, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return db_unavailable();
    }

    const auto settings = db::get_dashboard_settings(handle.db);
    if (!settings.has_value()) {
        return settings_unavailable();
    }
    if (!settings->password_hash.has_value() || settings->password_hash->empty()) {
        return {
            .status = 400,
            .code = "password_not_configured",
            .message = "Password is not configured",
        };
    }

    if (!auth::dashboard::verify_password(password, *settings->password_hash)) {
        return {
            .status = 401,
            .code = "invalid_credentials",
            .message = "Invalid credentials",
        };
    }

    const auto session_id = session_manager().create(/*password_verified=*/true, /*totp_verified=*/false, now_ms());
    auto response = get_dashboard_auth_session(session_id, handle.db);
    response.session_id = session_id;
    return response;
}

TotpSetupStartResponse start_totp_setup(const std::string_view session_id, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }

    const auto settings = db::get_dashboard_settings(handle.db);
    if (!settings.has_value()) {
        return {
            .status = 500,
            .code = "settings_unavailable",
            .message = "Failed to load dashboard auth settings",
        };
    }

    const auto session = session_manager().get(session_id, now_ms());
    if (!session.has_value() || !session->password_verified) {
        return {
            .status = 401,
            .code = "auth_required",
            .message = "Authentication is required",
        };
    }
    if (settings->totp_secret.has_value() && !settings->totp_secret->empty()) {
        return {
            .status = 400,
            .code = "invalid_totp_setup",
            .message = "TOTP is already configured",
        };
    }

    const auto secret = auth::dashboard::generate_totp_secret();
    if (secret.empty()) {
        return {
            .status = 500,
            .code = "totp_secret_generation_failed",
            .message = "Failed to generate TOTP secret",
        };
    }

    pending_totp_secret_by_session()[std::string(session_id)] = secret;
    return {
        .status = 200,
        .secret = secret,
        .otpauth_uri = auth::dashboard::build_totp_otpauth_uri(secret, "tightrope", "dashboard"),
    };
}

DashboardAuthResponse
confirm_totp_setup(const std::string_view session_id, const std::string_view secret, const std::string_view code, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return db_unavailable();
    }

    const auto session = session_manager().get(session_id, now_ms());
    if (!session.has_value() || !session->password_verified) {
        return auth_required();
    }

    const auto pending_it = pending_totp_secret_by_session().find(std::string(session_id));
    if (pending_it == pending_totp_secret_by_session().end() || pending_it->second != secret) {
        return {
            .status = 400,
            .code = "invalid_totp_setup",
            .message = "Invalid TOTP setup payload",
        };
    }

    if (!auth::dashboard::verify_totp_code(secret, code, now_unix_seconds(), /*window=*/1, std::nullopt, nullptr)) {
        return {
            .status = 400,
            .code = "invalid_totp_code",
            .message = "Invalid TOTP code",
        };
    }

    if (!db::set_dashboard_totp_secret(handle.db, secret)) {
        return {
            .status = 500,
            .code = "totp_store_failed",
            .message = "Failed to persist TOTP secret",
        };
    }
    pending_totp_secret_by_session().erase(std::string(session_id));
    return get_dashboard_auth_session(session_id, handle.db);
}

DashboardAuthResponse verify_dashboard_totp(const std::string_view session_id, const std::string_view code, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return db_unavailable();
    }

    const auto settings = db::get_dashboard_settings(handle.db);
    if (!settings.has_value()) {
        return settings_unavailable();
    }

    const auto session = session_manager().get(session_id, now_ms());
    if (!session.has_value() || !session->password_verified) {
        return auth_required();
    }
    if (!settings->totp_secret.has_value() || settings->totp_secret->empty()) {
        return {
            .status = 400,
            .code = "invalid_totp_code",
            .message = "TOTP is not configured",
        };
    }

    std::int64_t matched_step = 0;
    if (!auth::dashboard::verify_totp_code(
            *settings->totp_secret,
            code,
            now_unix_seconds(),
            /*window=*/1,
            settings->totp_last_verified_step,
            &matched_step
        )) {
        return {
            .status = 400,
            .code = "invalid_totp_code",
            .message = "Invalid TOTP code",
        };
    }

    if (!db::advance_dashboard_totp_last_verified_step(handle.db, matched_step)) {
        return {
            .status = 400,
            .code = "invalid_totp_code",
            .message = "Invalid TOTP code",
        };
    }

    const auto new_session_id = session_manager().create(/*password_verified=*/true, /*totp_verified=*/true, now_ms());
    auto response = get_dashboard_auth_session(new_session_id, handle.db);
    response.session_id = new_session_id;
    return response;
}

void logout_dashboard(const std::string_view session_id) {
    if (session_id.empty()) {
        return;
    }
    session_manager().erase(session_id);
    pending_totp_secret_by_session().erase(std::string(session_id));
}

} // namespace tightrope::server::controllers
