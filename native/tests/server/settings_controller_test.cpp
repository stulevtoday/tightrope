#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <cstdlib>

#include <sqlite3.h>

#include "migration/migration_runner.h"
#include "controllers/settings_controller.h"

namespace {

std::string make_temp_db_path() {
    const auto file = std::filesystem::temp_directory_path() / std::filesystem::path("tightrope-settings-controller.sqlite3");
    std::filesystem::remove(file);
    return file.string();
}

} // namespace

TEST_CASE("settings controller returns defaults and supports updates", "[server][settings]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    const auto initial = tightrope::server::controllers::get_settings(db);
    REQUIRE(initial.status == 200);
    REQUIRE(initial.settings.theme == "auto");
    REQUIRE(initial.settings.upstream_stream_transport == "default");
    REQUIRE(initial.settings.routing_strategy == "usage_weighted");
    REQUIRE(initial.settings.routing_headroom_weight_primary == Catch::Approx(0.35));
    REQUIRE(initial.settings.routing_headroom_weight_secondary == Catch::Approx(0.65));
    REQUIRE(initial.settings.routing_score_alpha == Catch::Approx(0.3));
    REQUIRE(initial.settings.routing_score_beta == Catch::Approx(0.25));
    REQUIRE(initial.settings.routing_score_gamma == Catch::Approx(0.2));
    REQUIRE(initial.settings.routing_score_delta == Catch::Approx(0.2));
    REQUIRE(initial.settings.routing_score_zeta == Catch::Approx(0.05));
    REQUIRE(initial.settings.routing_score_eta == Catch::Approx(1.0));
    REQUIRE(initial.settings.routing_success_rate_rho == Catch::Approx(2.0));
    REQUIRE(initial.settings.sync_cluster_name == "default");
    REQUIRE(initial.settings.sync_site_id == 1);
    REQUIRE(initial.settings.sync_port == 9400);
    REQUIRE(initial.settings.sync_discovery_enabled);
    REQUIRE(initial.settings.sync_interval_seconds == 5);
    REQUIRE(initial.settings.sync_conflict_resolution == "lww");
    REQUIRE(initial.settings.sync_journal_retention_days == 30);
    REQUIRE(initial.settings.sync_tls_enabled);
    REQUIRE_FALSE(initial.settings.totp_configured);
    REQUIRE_FALSE(initial.settings.totp_required_on_login);

    tightrope::server::controllers::DashboardSettingsUpdate patch;
    patch.theme = "dark";
    patch.sticky_threads_enabled = true;
    patch.upstream_stream_transport = "websocket";
    patch.prefer_earlier_reset_accounts = true;
    patch.routing_strategy = "round_robin";
    patch.openai_cache_affinity_max_age_seconds = 900;
    patch.import_without_overwrite = true;
    patch.api_key_auth_enabled = true;
    patch.totp_required_on_login = true;
    patch.routing_headroom_weight_primary = 0.45;
    patch.routing_headroom_weight_secondary = 0.55;
    patch.routing_score_alpha = 0.21;
    patch.routing_score_beta = 0.19;
    patch.routing_score_gamma = 0.18;
    patch.routing_score_delta = 0.17;
    patch.routing_score_zeta = 0.09;
    patch.routing_score_eta = 0.16;
    patch.routing_success_rate_rho = 2.7;
    patch.sync_cluster_name = "tightrope-prod";
    patch.sync_site_id = 22;
    patch.sync_port = 9577;
    patch.sync_discovery_enabled = false;
    patch.sync_interval_seconds = 13;
    patch.sync_conflict_resolution = "site_priority";
    patch.sync_journal_retention_days = 45;
    patch.sync_tls_enabled = false;

    const auto invalid = tightrope::server::controllers::update_settings(patch, db);
    REQUIRE(invalid.status == 400);
    REQUIRE(invalid.code == "invalid_totp_config");

    patch.totp_required_on_login = false;
    const auto updated = tightrope::server::controllers::update_settings(patch, db);
    REQUIRE(updated.status == 200);
    REQUIRE(updated.settings.theme == "dark");
    REQUIRE(updated.settings.sticky_threads_enabled);
    REQUIRE(updated.settings.upstream_stream_transport == "websocket");
    REQUIRE(updated.settings.prefer_earlier_reset_accounts);
    REQUIRE(updated.settings.routing_strategy == "round_robin");
    REQUIRE(updated.settings.openai_cache_affinity_max_age_seconds == 900);
    REQUIRE(updated.settings.import_without_overwrite);
    REQUIRE(updated.settings.api_key_auth_enabled);
    REQUIRE(updated.settings.routing_headroom_weight_primary == Catch::Approx(0.45));
    REQUIRE(updated.settings.routing_headroom_weight_secondary == Catch::Approx(0.55));
    REQUIRE(updated.settings.routing_score_alpha == Catch::Approx(0.21));
    REQUIRE(updated.settings.routing_score_beta == Catch::Approx(0.19));
    REQUIRE(updated.settings.routing_score_gamma == Catch::Approx(0.18));
    REQUIRE(updated.settings.routing_score_delta == Catch::Approx(0.17));
    REQUIRE(updated.settings.routing_score_zeta == Catch::Approx(0.09));
    REQUIRE(updated.settings.routing_score_eta == Catch::Approx(0.16));
    REQUIRE(updated.settings.routing_success_rate_rho == Catch::Approx(2.7));
    REQUIRE(updated.settings.sync_cluster_name == "tightrope-prod");
    REQUIRE(updated.settings.sync_site_id == 22);
    REQUIRE(updated.settings.sync_port == 9577);
    REQUIRE_FALSE(updated.settings.sync_discovery_enabled);
    REQUIRE(updated.settings.sync_interval_seconds == 13);
    REQUIRE(updated.settings.sync_conflict_resolution == "site_priority");
    REQUIRE(updated.settings.sync_journal_retention_days == 45);
    REQUIRE_FALSE(updated.settings.sync_tls_enabled);

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE("settings controller validates sync and routing tuning fields", "[server][settings]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::server::controllers::DashboardSettingsUpdate patch;
    patch.theme = "invalid-theme";
    auto response = tightrope::server::controllers::update_settings(patch, db);
    REQUIRE(response.status == 400);
    REQUIRE(response.code == "invalid_theme");

    patch = {};
    patch.sync_port = 0;
    response = tightrope::server::controllers::update_settings(patch, db);
    REQUIRE(response.status == 400);
    REQUIRE(response.code == "invalid_sync_port");

    patch = {};
    patch.sync_conflict_resolution = "invalid";
    response = tightrope::server::controllers::update_settings(patch, db);
    REQUIRE(response.status == 400);
    REQUIRE(response.code == "invalid_sync_conflict_resolution");

    patch = {};
    patch.routing_score_alpha = 1.5;
    response = tightrope::server::controllers::update_settings(patch, db);
    REQUIRE(response.status == 400);
    REQUIRE(response.code == "invalid_routing_weight");

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE("runtime connect address resolves non-loopback host and defaults for loopback", "[server][settings]") {
    REQUIRE(
        tightrope::server::controllers::get_runtime_connect_address("10.1.2.3").connect_address == "10.1.2.3");
    REQUIRE(
        tightrope::server::controllers::get_runtime_connect_address("localhost").connect_address == "<tightrope-ip-or-dns>");
}

TEST_CASE("runtime connect address uses tightrope env override", "[server][settings]") {
    const char* tightrope_original = std::getenv("TIGHTROPE_CONNECT_ADDRESS");
    const std::optional<std::string> tightrope_saved =
        tightrope_original != nullptr ? std::optional<std::string>{tightrope_original} : std::nullopt;

    REQUIRE(::setenv("TIGHTROPE_CONNECT_ADDRESS", "tightrope.example.test", 1) == 0);

    const auto response = tightrope::server::controllers::get_runtime_connect_address("localhost");
    REQUIRE(response.status == 200);
    REQUIRE(response.connect_address == "tightrope.example.test");

    if (tightrope_saved.has_value()) {
        (void)::setenv("TIGHTROPE_CONNECT_ADDRESS", tightrope_saved->c_str(), 1);
    } else {
        (void)::unsetenv("TIGHTROPE_CONNECT_ADDRESS");
    }
}
