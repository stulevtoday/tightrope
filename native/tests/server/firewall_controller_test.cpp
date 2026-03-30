#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include <sqlite3.h>

#include "migration/migration_runner.h"
#include "controllers/firewall_controller.h"

namespace {

std::string make_temp_db_path() {
    const auto file =
        std::filesystem::temp_directory_path() / std::filesystem::path("tightrope-firewall-controller.sqlite3");
    std::filesystem::remove(file);
    return file.string();
}

} // namespace

TEST_CASE("firewall controller supports mode transitions and CRUD errors", "[server][firewall]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    const auto initial = tightrope::server::controllers::list_firewall_ips(db);
    REQUIRE(initial.status == 200);
    REQUIRE(initial.mode == "allow_all");
    REQUIRE(initial.entries.empty());

    const auto created = tightrope::server::controllers::add_firewall_ip("127.0.0.1", db);
    REQUIRE(created.status == 200);
    REQUIRE(created.code.empty());
    REQUIRE(created.entry.ip_address == "127.0.0.1");
    REQUIRE_FALSE(created.entry.created_at.empty());

    const auto duplicate = tightrope::server::controllers::add_firewall_ip("127.0.0.1", db);
    REQUIRE(duplicate.status == 409);
    REQUIRE(duplicate.code == "ip_exists");

    const auto invalid_create = tightrope::server::controllers::add_firewall_ip("not-an-ip", db);
    REQUIRE(invalid_create.status == 400);
    REQUIRE(invalid_create.code == "invalid_ip");

    const auto listed = tightrope::server::controllers::list_firewall_ips(db);
    REQUIRE(listed.status == 200);
    REQUIRE(listed.mode == "allowlist_active");
    REQUIRE(listed.entries.size() == 1);
    REQUIRE(listed.entries.front().ip_address == "127.0.0.1");

    const auto invalid_delete = tightrope::server::controllers::delete_firewall_ip("not-an-ip", db);
    REQUIRE(invalid_delete.status == 400);
    REQUIRE(invalid_delete.code == "invalid_ip");

    const auto missing_delete = tightrope::server::controllers::delete_firewall_ip("203.0.113.44", db);
    REQUIRE(missing_delete.status == 404);
    REQUIRE(missing_delete.code == "ip_not_found");

    const auto deleted = tightrope::server::controllers::delete_firewall_ip("127.0.0.1", db);
    REQUIRE(deleted.status == 200);
    REQUIRE(deleted.result == "deleted");

    const auto final_list = tightrope::server::controllers::list_firewall_ips(db);
    REQUIRE(final_list.status == 200);
    REQUIRE(final_list.mode == "allow_all");
    REQUIRE(final_list.entries.empty());

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE("firewall controller normalizes IPv6 text form", "[server][firewall]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    const auto created = tightrope::server::controllers::add_firewall_ip(
        "2001:0db8:0000:0000:0000:ff00:0042:8329",
        db
    );
    REQUIRE(created.status == 200);
    REQUIRE(created.entry.ip_address == "2001:db8::ff00:42:8329");

    const auto deleted = tightrope::server::controllers::delete_firewall_ip("2001:db8::ff00:42:8329", db);
    REQUIRE(deleted.status == 200);
    REQUIRE(deleted.result == "deleted");

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}
