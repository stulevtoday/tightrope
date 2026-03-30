#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "server.h"
#include "server/runtime_test_utils.h"

TEST_CASE("uwebsockets runtime serves health and models routes", "[server][runtime]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const auto health_response = tightrope::tests::server::send_raw_http(
        port,
        "GET /health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(health_response.find("200 OK") != std::string::npos);
    REQUIRE(health_response.find("\"status\":\"ok\"") != std::string::npos);
    REQUIRE(health_response.find("\"uptime_ms\":") != std::string::npos);

    const auto models_response = tightrope::tests::server::send_raw_http(
        port,
        "GET /v1/models HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(models_response.find("200 OK") != std::string::npos);
    REQUIRE(models_response.find("\"object\":\"list\"") != std::string::npos);
    REQUIRE(models_response.find("\"data\"") != std::string::npos);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("uwebsockets runtime stop is idempotent", "[server][runtime]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));
    REQUIRE(runtime.stop());
    REQUIRE(runtime.stop());

    std::filesystem::remove(db_path);
}

TEST_CASE("uwebsockets runtime upgrades websocket responses route", "[server][runtime][ws]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const auto handshake_response = tightrope::tests::server::send_raw_http(
        port,
        "GET /v1/responses HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: Upgrade\r\n"
        "Upgrade: websocket\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n",
        4
    );

    REQUIRE(handshake_response.find("101 Switching Protocols") != std::string::npos);
    const bool has_accept_header = handshake_response.find("Sec-WebSocket-Accept") != std::string::npos ||
                                   handshake_response.find("sec-websocket-accept") != std::string::npos;
    REQUIRE(has_accept_header);
    const auto turn_state = tightrope::tests::server::http_header_value(handshake_response, "x-codex-turn-state");
    REQUIRE(turn_state.has_value());
    REQUIRE(turn_state->rfind("http_turn_", 0) == 0);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("uwebsockets runtime serves firewall CRUD endpoints", "[server][runtime][firewall]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const auto initial = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/firewall/ips HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(initial.find("200 OK") != std::string::npos);
    REQUIRE(initial.find("\"mode\":\"allow_all\"") != std::string::npos);

    const std::string create_body = R"({"ipAddress":"127.0.0.1"})";
    const auto created = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/firewall/ips HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(create_body.size()) + "\r\n\r\n" + create_body
    );
    REQUIRE(created.find("200 OK") != std::string::npos);
    REQUIRE(created.find("\"ipAddress\":\"127.0.0.1\"") != std::string::npos);
    REQUIRE(created.find("\"createdAt\":") != std::string::npos);

    const auto listed = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/firewall/ips HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(listed.find("200 OK") != std::string::npos);
    REQUIRE(listed.find("\"mode\":\"allowlist_active\"") != std::string::npos);
    REQUIRE(listed.find("\"ipAddress\":\"127.0.0.1\"") != std::string::npos);

    const auto deleted = tightrope::tests::server::send_raw_http(
        port,
        "DELETE /api/firewall/ips/127.0.0.1 HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(deleted.find("200 OK") != std::string::npos);
    REQUIRE(deleted.find("\"status\":\"deleted\"") != std::string::npos);

    const auto final_state = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/firewall/ips HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(final_state.find("200 OK") != std::string::npos);
    REQUIRE(final_state.find("\"mode\":\"allow_all\"") != std::string::npos);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("uwebsockets runtime enforces firewall on protected routes", "[server][runtime][firewall]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const std::string blocked_ip_body = R"({"ipAddress":"10.20.30.40"})";
    const auto add_blocked = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/firewall/ips HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(blocked_ip_body.size()) + "\r\n\r\n" + blocked_ip_body
    );
    REQUIRE(add_blocked.find("200 OK") != std::string::npos);

    const auto blocked = tightrope::tests::server::send_raw_http(
        port,
        "GET /v1/models HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(blocked.find("403 Forbidden") != std::string::npos);
    REQUIRE(blocked.find("\"ip_forbidden\"") != std::string::npos);

    const std::string loopback_body = R"({"ipAddress":"127.0.0.1"})";
    const auto add_loopback = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/firewall/ips HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(loopback_body.size()) + "\r\n\r\n" + loopback_body
    );
    REQUIRE(add_loopback.find("200 OK") != std::string::npos);

    const auto allowed = tightrope::tests::server::send_raw_http(
        port,
        "GET /v1/models HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(allowed.find("200 OK") != std::string::npos);
    REQUIRE(allowed.find("\"object\":\"list\"") != std::string::npos);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("uwebsockets runtime blocks websocket upgrades when firewall denies access", "[server][runtime][firewall][ws]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const std::string create_body = R"({"ipAddress":"10.20.30.40"})";
    const auto created = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/firewall/ips HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(create_body.size()) + "\r\n\r\n" + create_body
    );
    REQUIRE(created.find("200 OK") != std::string::npos);

    const auto handshake_response = tightrope::tests::server::send_raw_http(
        port,
        "GET /v1/responses HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: Upgrade\r\n"
        "Upgrade: websocket\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n",
        4
    );

    REQUIRE(handshake_response.find("403 Forbidden") != std::string::npos);
    REQUIRE(handshake_response.find("\"ip_forbidden\"") != std::string::npos);
    REQUIRE(handshake_response.find("101 Switching Protocols") == std::string::npos);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}
