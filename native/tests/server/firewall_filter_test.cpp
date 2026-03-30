#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "migration/migration_runner.h"
#include "repositories/firewall_repo.h"
#include "openai/upstream_headers.h"
#include "middleware/firewall_filter.h"

namespace {

std::string make_temp_db_path() {
    const auto file = std::filesystem::temp_directory_path() / std::filesystem::path("tightrope-firewall-filter.sqlite3");
    std::filesystem::remove(file);
    return file.string();
}

} // namespace

TEST_CASE("firewall filter identifies protected paths", "[server][firewall]") {
    using tightrope::server::middleware::is_firewall_protected_path;

    REQUIRE(is_firewall_protected_path("/backend-api/codex"));
    REQUIRE(is_firewall_protected_path("/backend-api/codex/models"));
    REQUIRE(is_firewall_protected_path("/v1"));
    REQUIRE(is_firewall_protected_path("/v1/models"));

    REQUIRE_FALSE(is_firewall_protected_path("/api/settings"));
    REQUIRE_FALSE(is_firewall_protected_path("/api/codex/usage"));
    REQUIRE_FALSE(is_firewall_protected_path("/health"));
    REQUIRE_FALSE(is_firewall_protected_path("/v11/models"));
}

TEST_CASE("firewall filter resolves client ip from socket and trusted proxy chains", "[server][firewall]") {
    using tightrope::proxy::openai::HeaderMap;
    using tightrope::server::middleware::parse_trusted_proxy_networks;
    using tightrope::server::middleware::resolve_connection_client_ip;

    const auto trusted_loopback = parse_trusted_proxy_networks({"127.0.0.1/32"});
    const auto trusted_private = parse_trusted_proxy_networks({"10.0.0.0/8"});

    HeaderMap single_forwarded = {{"x-forwarded-for", "198.51.100.10"}};
    REQUIRE(
        resolve_connection_client_ip(
            single_forwarded,
            std::optional<std::string_view>("127.0.0.1"),
            false,
            trusted_loopback
        ) == std::optional<std::string>("127.0.0.1")
    );
    REQUIRE(
        resolve_connection_client_ip(
            single_forwarded,
            std::optional<std::string_view>("127.0.0.1"),
            true,
            trusted_loopback
        ) == std::optional<std::string>("198.51.100.10")
    );

    HeaderMap rightmost_untrusted = {{"x-forwarded-for", "10.10.10.10, 198.51.100.10"}};
    REQUIRE(
        resolve_connection_client_ip(
            rightmost_untrusted,
            std::optional<std::string_view>("127.0.0.1"),
            true,
            trusted_loopback
        ) == std::optional<std::string>("198.51.100.10")
    );

    HeaderMap trusted_chain = {{"x-forwarded-for", "198.51.100.10, 10.0.0.1"}};
    REQUIRE(
        resolve_connection_client_ip(
            trusted_chain,
            std::optional<std::string_view>("10.0.0.2"),
            true,
            trusted_private
        ) == std::optional<std::string>("198.51.100.10")
    );

    HeaderMap invalid_chain = {{"x-forwarded-for", "198.51.100.10, not-an-ip"}};
    REQUIRE(
        resolve_connection_client_ip(
            invalid_chain,
            std::optional<std::string_view>("127.0.0.1"),
            true,
            trusted_loopback
        ) == std::optional<std::string>("127.0.0.1")
    );

    REQUIRE_FALSE(
        resolve_connection_client_ip(
            single_forwarded,
            std::nullopt,
            true,
            trusted_loopback
        )
        .has_value()
    );
}

TEST_CASE("firewall filter enforces allowlist only on protected routes", "[server][firewall]") {
    using tightrope::proxy::openai::HeaderMap;
    using tightrope::server::middleware::FirewallFilterConfig;
    using tightrope::server::middleware::evaluate_firewall_request;
    using tightrope::server::middleware::parse_trusted_proxy_networks;

    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    const HeaderMap empty_headers{};

    const auto allow_all = evaluate_firewall_request(
        db,
        "/v1/models",
        empty_headers,
        std::optional<std::string_view>("127.0.0.1")
    );
    REQUIRE(allow_all.allow);

    REQUIRE(
        tightrope::db::add_firewall_allowlist_ip(db, "10.20.30.40", nullptr) ==
        tightrope::db::FirewallWriteResult::Created
    );

    const auto blocked = evaluate_firewall_request(
        db,
        "/v1/models",
        empty_headers,
        std::optional<std::string_view>("127.0.0.1")
    );
    REQUIRE_FALSE(blocked.allow);
    REQUIRE(blocked.status == 403);
    REQUIRE(blocked.body.find("\"ip_forbidden\"") != std::string::npos);

    const auto allowed = evaluate_firewall_request(
        db,
        "/backend-api/codex/models",
        empty_headers,
        std::optional<std::string_view>("10.20.30.40")
    );
    REQUIRE(allowed.allow);

    const auto dashboard_bypass = evaluate_firewall_request(
        db,
        "/api/settings",
        empty_headers,
        std::optional<std::string_view>("127.0.0.1")
    );
    REQUIRE(dashboard_bypass.allow);

    const auto codex_usage_bypass = evaluate_firewall_request(
        db,
        "/api/codex/usage",
        empty_headers,
        std::optional<std::string_view>("127.0.0.1")
    );
    REQUIRE(codex_usage_bypass.allow);

    REQUIRE(
        tightrope::db::add_firewall_allowlist_ip(db, "198.51.100.10", nullptr) ==
        tightrope::db::FirewallWriteResult::Created
    );

    FirewallFilterConfig config;
    config.trust_proxy_headers = true;
    config.trusted_proxy_networks = parse_trusted_proxy_networks({"127.0.0.1/32"});
    const HeaderMap forwarded_headers = {{"x-forwarded-for", "198.51.100.10"}};

    const auto trusted_forwarded = evaluate_firewall_request(
        db,
        "/v1/models",
        forwarded_headers,
        std::optional<std::string_view>("127.0.0.1"),
        config
    );
    REQUIRE(trusted_forwarded.allow);

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}
