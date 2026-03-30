#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>

#include <sqlite3.h>

#include "migration/migration_runner.h"
#include "repositories/firewall_repo.h"

namespace {

std::string make_temp_db_path() {
    const auto file = std::filesystem::temp_directory_path() / std::filesystem::path("tightrope-firewall-repo.sqlite3");
    std::filesystem::remove(file);
    return file.string();
}

} // namespace

TEST_CASE("firewall repository enforces normalized allowlist CRUD", "[db][firewall]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    const auto initial = tightrope::db::list_firewall_allowlist_entries(db);
    REQUIRE(initial.empty());
    REQUIRE(tightrope::db::is_firewall_ip_allowed(db, std::nullopt));

    tightrope::db::FirewallIpRecord ipv4;
    REQUIRE(
        tightrope::db::add_firewall_allowlist_ip(db, "127.0.0.1", &ipv4) ==
        tightrope::db::FirewallWriteResult::Created
    );
    REQUIRE(ipv4.ip_address == "127.0.0.1");
    REQUIRE_FALSE(ipv4.created_at.empty());

    tightrope::db::FirewallIpRecord ipv6;
    REQUIRE(
        tightrope::db::add_firewall_allowlist_ip(db, "2001:0db8:0000:0000:0000:ff00:0042:8329", &ipv6) ==
        tightrope::db::FirewallWriteResult::Created
    );
    REQUIRE(ipv6.ip_address == "2001:db8::ff00:42:8329");

    REQUIRE(
        tightrope::db::add_firewall_allowlist_ip(db, "127.0.0.1", nullptr) ==
        tightrope::db::FirewallWriteResult::Duplicate
    );
    REQUIRE(
        tightrope::db::add_firewall_allowlist_ip(db, "not-an-ip", nullptr) ==
        tightrope::db::FirewallWriteResult::InvalidIp
    );

    const auto listed = tightrope::db::list_firewall_allowlist_entries(db);
    REQUIRE(listed.size() == 2);
    REQUIRE(listed[0].ip_address == "127.0.0.1");
    REQUIRE(listed[1].ip_address == "2001:db8::ff00:42:8329");

    REQUIRE(tightrope::db::is_firewall_ip_allowed(db, std::optional<std::string_view>("127.0.0.1")));
    REQUIRE(tightrope::db::is_firewall_ip_allowed(
        db,
        std::optional<std::string_view>("2001:0db8:0000:0000:0000:ff00:0042:8329")
    ));
    REQUIRE_FALSE(tightrope::db::is_firewall_ip_allowed(db, std::optional<std::string_view>("203.0.113.5")));
    REQUIRE_FALSE(tightrope::db::is_firewall_ip_allowed(db, std::optional<std::string_view>("invalid-ip")));
    REQUIRE_FALSE(tightrope::db::is_firewall_ip_allowed(db, std::nullopt));

    REQUIRE(
        tightrope::db::delete_firewall_allowlist_ip(db, "invalid-ip") ==
        tightrope::db::FirewallWriteResult::InvalidIp
    );
    REQUIRE(
        tightrope::db::delete_firewall_allowlist_ip(db, "203.0.113.5") ==
        tightrope::db::FirewallWriteResult::NotFound
    );
    REQUIRE(
        tightrope::db::delete_firewall_allowlist_ip(db, "127.0.0.1") ==
        tightrope::db::FirewallWriteResult::Deleted
    );
    REQUIRE(
        tightrope::db::delete_firewall_allowlist_ip(db, "2001:db8::ff00:42:8329") ==
        tightrope::db::FirewallWriteResult::Deleted
    );

    REQUIRE(tightrope::db::list_firewall_allowlist_entries(db).empty());
    REQUIRE(tightrope::db::is_firewall_ip_allowed(db, std::nullopt));

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}
