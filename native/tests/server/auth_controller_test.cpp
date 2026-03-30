#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>

#include <sqlite3.h>

#include "dashboard/totp_auth.h"
#include "migration/migration_runner.h"
#include "controllers/auth_controller.h"
#include "controllers/settings_controller.h"

namespace {

std::string make_temp_db_path() {
    const auto file = std::filesystem::temp_directory_path() / std::filesystem::path("tightrope-auth-controller.sqlite3");
    std::filesystem::remove(file);
    return file.string();
}

std::int64_t now_unix_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

TEST_CASE("dashboard auth controller enforces password and totp flow", "[server][auth]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    const auto before = tightrope::server::controllers::get_dashboard_auth_session("", db);
    REQUIRE(before.status == 200);
    REQUIRE(before.session.authenticated);
    REQUIRE_FALSE(before.session.password_required);

    const auto short_pw = tightrope::server::controllers::setup_dashboard_password("short", db);
    REQUIRE(short_pw.status == 400);
    REQUIRE(short_pw.code == "invalid_password");

    const auto setup = tightrope::server::controllers::setup_dashboard_password("very-secure-password", db);
    REQUIRE(setup.status == 200);
    REQUIRE_FALSE(setup.session_id.empty());
    REQUIRE(setup.session.authenticated);
    REQUIRE(setup.session.password_required);
    REQUIRE_FALSE(setup.session.totp_configured);

    tightrope::server::controllers::logout_dashboard(setup.session_id);
    const auto after_logout = tightrope::server::controllers::get_dashboard_auth_session("", db);
    REQUIRE(after_logout.status == 200);
    REQUIRE_FALSE(after_logout.session.authenticated);
    REQUIRE(after_logout.session.password_required);

    const auto bad_login = tightrope::server::controllers::login_dashboard_password("wrong-password", db);
    REQUIRE(bad_login.status == 401);
    REQUIRE(bad_login.code == "invalid_credentials");

    const auto login = tightrope::server::controllers::login_dashboard_password("very-secure-password", db);
    REQUIRE(login.status == 200);
    REQUIRE_FALSE(login.session_id.empty());
    REQUIRE(login.session.authenticated);
    REQUIRE(login.session.password_required);

    const auto totp_start = tightrope::server::controllers::start_totp_setup(login.session_id, db);
    REQUIRE(totp_start.status == 200);
    REQUIRE_FALSE(totp_start.secret.empty());
    REQUIRE(totp_start.otpauth_uri.find("otpauth://totp/") == 0);

    const auto code = tightrope::auth::dashboard::generate_totp_code(totp_start.secret, now_unix_seconds());
    REQUIRE(code.has_value());

    const auto confirmed =
        tightrope::server::controllers::confirm_totp_setup(login.session_id, totp_start.secret, *code, db);
    REQUIRE(confirmed.status == 200);
    REQUIRE(confirmed.session.totp_configured);

    tightrope::server::controllers::DashboardSettingsUpdate patch;
    patch.totp_required_on_login = true;
    patch.sticky_threads_enabled = false;
    patch.prefer_earlier_reset_accounts = false;
    const auto settings = tightrope::server::controllers::update_settings(patch, db);
    REQUIRE(settings.status == 200);
    REQUIRE(settings.settings.totp_required_on_login);

    const auto totp_required_session = tightrope::server::controllers::get_dashboard_auth_session(login.session_id, db);
    REQUIRE(totp_required_session.status == 200);
    REQUIRE_FALSE(totp_required_session.session.authenticated);
    REQUIRE(totp_required_session.session.totp_required_on_login);

    const auto verify = tightrope::server::controllers::verify_dashboard_totp(login.session_id, *code, db);
    REQUIRE(verify.status == 200);
    REQUIRE_FALSE(verify.session_id.empty());
    REQUIRE(verify.session.authenticated);
    REQUIRE(verify.session.totp_configured);

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}
