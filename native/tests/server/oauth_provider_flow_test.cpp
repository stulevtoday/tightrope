#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "oauth/oauth_service.h"
#include "server.h"
#include "server/oauth_provider_fake.h"
#include "server/runtime_test_utils.h"

namespace {

std::optional<std::string> json_string_field(const std::string_view body, const std::string_view key) {
    const std::string prefix = std::string("\"") + std::string(key) + "\":\"";
    const auto start = body.find(prefix);
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    const auto value_start = start + prefix.size();
    const auto value_end = body.find('"', value_start);
    if (value_end == std::string_view::npos) {
        return std::nullopt;
    }
    return std::string(body.substr(value_start, value_end - value_start));
}

std::optional<std::string> query_param(const std::string_view url, const std::string_view key) {
    const auto question = url.find('?');
    if (question == std::string_view::npos || question + 1 >= url.size()) {
        return std::nullopt;
    }
    const std::string needle = std::string(key) + "=";
    std::size_t cursor = question + 1;
    while (cursor < url.size()) {
        const auto end = url.find('&', cursor);
        const auto token_end = end == std::string_view::npos ? url.size() : end;
        const auto token = url.substr(cursor, token_end - cursor);
        if (token.rfind(needle, 0) == 0) {
            return std::string(token.substr(needle.size()));
        }
        if (end == std::string_view::npos) {
            break;
        }
        cursor = end + 1;
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("oauth manual callback exchanges token and persists account", "[server][runtime][admin][oauth][provider]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    tightrope::tests::server::EnvVarGuard redirect_guard{"TIGHTROPE_OAUTH_REDIRECT_URI"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));
    REQUIRE(redirect_guard.set("http://localhost:1455/auth/callback"));

    auto fake_provider = std::make_shared<tightrope::tests::server::OAuthProviderFake>("alice@example.com");
    tightrope::auth::oauth::set_provider_client_for_testing(fake_provider);
    tightrope::auth::oauth::reset_oauth_state_for_testing();

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const std::string start_browser_body = R"({"forceMethod":"browser"})";
    const auto start_browser = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/oauth/start HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(start_browser_body.size()) + "\r\n\r\n" + start_browser_body
    );
    REQUIRE(start_browser.find("200 OK") != std::string::npos);
    const auto start_body = tightrope::tests::server::http_body(start_browser);
    const auto authorization_url = json_string_field(start_body, "authorizationUrl");
    REQUIRE(authorization_url.has_value());
    const auto state_token = query_param(*authorization_url, "state");
    REQUIRE(state_token.has_value());

    const std::string manual_callback_ok = std::string(R"({"callbackUrl":"http://localhost:1455/auth/callback?code=manual-code&state=)") +
                                          *state_token + "\"}";
    const auto manual_success = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/oauth/manual-callback HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(manual_callback_ok.size()) + "\r\n\r\n" + manual_callback_ok
    );
    REQUIRE(manual_success.find("200 OK") != std::string::npos);
    REQUIRE(manual_success.find("\"status\":\"success\"") != std::string::npos);

    const auto list_accounts = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/accounts HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(list_accounts.find("200 OK") != std::string::npos);
    REQUIRE(list_accounts.find("\"email\":\"alice@example.com\"") != std::string::npos);
    REQUIRE(fake_provider->authorization_exchange_calls() == 1);

    REQUIRE(runtime.stop());
    tightrope::auth::oauth::clear_provider_client_for_testing();
    tightrope::auth::oauth::reset_oauth_state_for_testing();
    std::filesystem::remove(db_path);
}

TEST_CASE("oauth browser callback endpoint completes flow and persists account", "[server][runtime][admin][oauth][provider]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    tightrope::tests::server::EnvVarGuard redirect_guard{"TIGHTROPE_OAUTH_REDIRECT_URI"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));
    REQUIRE(redirect_guard.set("http://localhost:1455/auth/callback"));

    auto fake_provider = std::make_shared<tightrope::tests::server::OAuthProviderFake>("browser@example.com");
    tightrope::auth::oauth::set_provider_client_for_testing(fake_provider);
    tightrope::auth::oauth::reset_oauth_state_for_testing();

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const std::string start_browser_body = R"({"forceMethod":"browser"})";
    const auto start_browser = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/oauth/start HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(start_browser_body.size()) + "\r\n\r\n" + start_browser_body
    );
    REQUIRE(start_browser.find("200 OK") != std::string::npos);
    const auto start_body = tightrope::tests::server::http_body(start_browser);
    const auto authorization_url = json_string_field(start_body, "authorizationUrl");
    REQUIRE(authorization_url.has_value());
    const auto state_token = query_param(*authorization_url, "state");
    REQUIRE(state_token.has_value());

    const auto callback_response = tightrope::tests::server::send_raw_http(
        port,
        "GET /auth/callback?code=browser-code&state=" + *state_token +
            " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(callback_response.find("200 OK") != std::string::npos);
    REQUIRE(callback_response.find("Authorization complete. You can close this tab.") != std::string::npos);
    REQUIRE(callback_response.find("tightrope://oauth/success") != std::string::npos);
    REQUIRE(callback_response.find("window.close") != std::string::npos);

    const auto status_response = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/oauth/status HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(status_response.find("200 OK") != std::string::npos);
    REQUIRE(status_response.find("\"status\":\"success\"") != std::string::npos);

    const auto list_accounts = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/accounts HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(list_accounts.find("200 OK") != std::string::npos);
    REQUIRE(list_accounts.find("\"email\":\"browser@example.com\"") != std::string::npos);
    REQUIRE(fake_provider->authorization_exchange_calls() == 1);

    REQUIRE(runtime.stop());
    tightrope::auth::oauth::clear_provider_client_for_testing();
    tightrope::auth::oauth::reset_oauth_state_for_testing();
    std::filesystem::remove(db_path);
}

TEST_CASE("oauth device flow polls provider and persists account", "[server][runtime][admin][oauth][provider]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    auto fake_provider = std::make_shared<tightrope::tests::server::OAuthProviderFake>("device@example.com");
    tightrope::auth::oauth::set_provider_client_for_testing(fake_provider);
    tightrope::auth::oauth::reset_oauth_state_for_testing();

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const std::string start_device_body = R"({"forceMethod":"device"})";
    const auto start_device = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/oauth/start HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(start_device_body.size()) + "\r\n\r\n" + start_device_body
    );
    REQUIRE(start_device.find("200 OK") != std::string::npos);
    REQUIRE(start_device.find("\"method\":\"device\"") != std::string::npos);
    REQUIRE(start_device.find("\"deviceAuthId\":\"dev_test_1234\"") != std::string::npos);

    const std::string complete_body = R"({"deviceAuthId":"dev_test_1234","userCode":"ABCD-EFGH"})";
    const auto complete_response = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/oauth/complete HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(complete_body.size()) + "\r\n\r\n" + complete_body
    );
    REQUIRE(complete_response.find("200 OK") != std::string::npos);
    REQUIRE(complete_response.find("\"status\":\"pending\"") != std::string::npos);

    bool saw_success = false;
    std::string last_status_response;
    for (int attempt = 0; attempt < 20; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const auto status = tightrope::tests::server::send_raw_http(
            port,
            "GET /api/oauth/status HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
        );
        last_status_response = status;
        if (status.find("\"status\":\"success\"") != std::string::npos) {
            saw_success = true;
            break;
        }
    }
    INFO(last_status_response);
    REQUIRE(saw_success);

    const auto list_accounts = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/accounts HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(list_accounts.find("200 OK") != std::string::npos);
    REQUIRE(list_accounts.find("\"email\":\"device@example.com\"") != std::string::npos);
    REQUIRE(fake_provider->device_poll_calls() >= 3);

    REQUIRE(runtime.stop());
    tightrope::auth::oauth::clear_provider_client_for_testing();
    tightrope::auth::oauth::reset_oauth_state_for_testing();
    std::filesystem::remove(db_path);
}
