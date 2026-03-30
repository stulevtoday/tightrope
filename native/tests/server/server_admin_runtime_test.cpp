#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

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

bool json_bool_field(const std::string_view body, const std::string_view key, const bool value) {
    const std::string needle = std::string("\"") + std::string(key) + "\":" + (value ? "true" : "false");
    return body.find(needle) != std::string_view::npos;
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

TEST_CASE("uwebsockets runtime serves settings endpoints", "[server][runtime][admin][settings]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const auto initial = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/settings HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(initial.find("200 OK") != std::string::npos);
    REQUIRE(initial.find("\"theme\":\"auto\"") != std::string::npos);
    REQUIRE(initial.find("\"upstreamStreamTransport\"") != std::string::npos);
    REQUIRE(initial.find("\"routingStrategy\"") != std::string::npos);
    REQUIRE(initial.find("\"syncClusterName\"") != std::string::npos);
    REQUIRE(initial.find("\"routingScoreAlpha\"") != std::string::npos);

    const std::string patch_body =
        R"({"theme":"dark","stickyThreadsEnabled":true,"upstreamStreamTransport":"websocket","preferEarlierResetAccounts":true,)"
        R"("routingStrategy":"round_robin","openaiCacheAffinityMaxAgeSeconds":900,"importWithoutOverwrite":true,)"
        R"("totpRequiredOnLogin":false,"apiKeyAuthEnabled":true,"routingScoreAlpha":0.22,)"
        R"("routingScoreBeta":0.2,"routingScoreGamma":0.19,"routingScoreDelta":0.18,)"
        R"("routingScoreZeta":0.12,"routingScoreEta":0.09,"routingHeadroomWeightPrimary":0.4,)"
        R"("routingHeadroomWeightSecondary":0.6,"routingSuccessRateRho":2.5,"syncClusterName":"cluster-test",)"
        R"("syncSiteId":44,"syncPort":9901,"syncDiscoveryEnabled":false,"syncIntervalSeconds":11,)"
        R"("syncConflictResolution":"field_merge","syncJournalRetentionDays":60,"syncTlsEnabled":false})";
    const auto updated = tightrope::tests::server::send_raw_http(
        port,
        "PUT /api/settings HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(patch_body.size()) + "\r\n\r\n" + patch_body
    );
    REQUIRE(updated.find("200 OK") != std::string::npos);
    REQUIRE(updated.find("\"theme\":\"dark\"") != std::string::npos);
    REQUIRE(updated.find("\"upstreamStreamTransport\":\"websocket\"") != std::string::npos);
    REQUIRE(updated.find("\"routingStrategy\":\"round_robin\"") != std::string::npos);
    REQUIRE(updated.find("\"apiKeyAuthEnabled\":true") != std::string::npos);
    REQUIRE(updated.find("\"routingScoreAlpha\":0.22") != std::string::npos);
    REQUIRE(updated.find("\"syncClusterName\":\"cluster-test\"") != std::string::npos);
    REQUIRE(updated.find("\"syncSiteId\":44") != std::string::npos);
    REQUIRE(updated.find("\"syncConflictResolution\":\"field_merge\"") != std::string::npos);
    REQUIRE(updated.find("\"syncTlsEnabled\":false") != std::string::npos);

    const auto connect_address = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/settings/runtime/connect-address HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: close\r\n\r\n"
    );
    REQUIRE(connect_address.find("200 OK") != std::string::npos);
    REQUIRE(connect_address.find("\"connectAddress\":\"<tightrope-ip-or-dns>\"") != std::string::npos);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("uwebsockets runtime serves tightrope oauth routes", "[server][runtime][admin][oauth]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    tightrope::tests::server::EnvVarGuard redirect_guard{"TIGHTROPE_OAUTH_REDIRECT_URI"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));
    REQUIRE(redirect_guard.set("http://localhost:1455/auth/callback"));

    auto fake_provider = std::make_shared<tightrope::tests::server::OAuthProviderFake>("runtime@example.com");
    tightrope::auth::oauth::set_provider_client_for_testing(fake_provider);
    tightrope::auth::oauth::reset_oauth_state_for_testing();

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const auto status_initial = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/oauth/status HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(status_initial.find("200 OK") != std::string::npos);
    REQUIRE(status_initial.find("\"status\":\"pending\"") != std::string::npos);

    const std::string browser_start_body = R"({"forceMethod":"browser"})";
    const auto start_browser = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/oauth/start HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(browser_start_body.size()) + "\r\n\r\n" + browser_start_body
    );
    REQUIRE(start_browser.find("200 OK") != std::string::npos);
    const auto start_browser_body = tightrope::tests::server::http_body(start_browser);
    REQUIRE(start_browser_body.find("\"method\":\"browser\"") != std::string::npos);
    const auto authorization_url = json_string_field(start_browser_body, "authorizationUrl");
    REQUIRE(authorization_url.has_value());
    REQUIRE(query_param(*authorization_url, "response_type") == std::optional<std::string>{"code"});
    REQUIRE(query_param(*authorization_url, "client_id") == std::optional<std::string>{"app_EMoamEEZ73f0CkXaXp7hrann"});
    REQUIRE(
        query_param(*authorization_url, "redirect_uri") ==
        std::optional<std::string>{"http%3A%2F%2Flocalhost%3A1455%2Fauth%2Fcallback"}
    );
    REQUIRE(
        query_param(*authorization_url, "scope") == std::optional<std::string>{"openid%20profile%20email%20offline_access"}
    );
    REQUIRE(query_param(*authorization_url, "code_challenge").has_value());
    REQUIRE(query_param(*authorization_url, "code_challenge_method") == std::optional<std::string>{"S256"});
    REQUIRE(query_param(*authorization_url, "id_token_add_organizations") == std::optional<std::string>{"true"});
    REQUIRE(query_param(*authorization_url, "codex_cli_simplified_flow") == std::optional<std::string>{"true"});
    REQUIRE(query_param(*authorization_url, "originator") == std::optional<std::string>{"codex_chatgpt_desktop"});
    const auto callback_url = json_string_field(start_browser_body, "callbackUrl");
    REQUIRE(callback_url.has_value());
    REQUIRE(callback_url->find(":1455/auth/callback") != std::string::npos);
    auto state_token = query_param(*authorization_url, "state");
    REQUIRE(state_token.has_value());

    const auto status_running = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/oauth/status HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(status_running.find("200 OK") != std::string::npos);
    REQUIRE(status_running.find("\"listenerRunning\":true") != std::string::npos);

    const auto stop_browser = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/oauth/stop HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(stop_browser.find("200 OK") != std::string::npos);
    REQUIRE(stop_browser.find("\"status\":\"stopped\"") != std::string::npos);
    REQUIRE(stop_browser.find("\"listenerRunning\":false") != std::string::npos);

    const auto restart_browser = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/oauth/restart HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(restart_browser.find("200 OK") != std::string::npos);
    const auto restart_browser_body = tightrope::tests::server::http_body(restart_browser);
    REQUIRE(restart_browser_body.find("\"method\":\"browser\"") != std::string::npos);
    const auto restart_authorization_url = json_string_field(restart_browser_body, "authorizationUrl");
    REQUIRE(restart_authorization_url.has_value());
    const auto restart_callback_url = json_string_field(restart_browser_body, "callbackUrl");
    REQUIRE(restart_callback_url.has_value());
    REQUIRE(restart_callback_url->find(":1455/auth/callback") != std::string::npos);
    state_token = query_param(*restart_authorization_url, "state");
    REQUIRE(state_token.has_value());

    const auto status_restarted = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/oauth/status HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(status_restarted.find("200 OK") != std::string::npos);
    REQUIRE(status_restarted.find("\"listenerRunning\":true") != std::string::npos);

    const std::string manual_callback_invalid =
        R"({"callbackUrl":"http://localhost:1455/auth/callback?code=manual-code&state=wrong"})";
    const auto manual_error = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/oauth/manual-callback HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(manual_callback_invalid.size()) + "\r\n\r\n" + manual_callback_invalid
    );
    REQUIRE(manual_error.find("200 OK") != std::string::npos);
    REQUIRE(manual_error.find("\"status\":\"error\"") != std::string::npos);
    REQUIRE(manual_error.find("Invalid OAuth callback: state mismatch or missing code.") != std::string::npos);

    const auto status_error = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/oauth/status HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(status_error.find("200 OK") != std::string::npos);
    REQUIRE(status_error.find("\"status\":\"error\"") != std::string::npos);

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
    REQUIRE(manual_success.find("\"errorMessage\":null") != std::string::npos);

    const auto callback_success = tightrope::tests::server::send_raw_http(
        port,
        "GET /auth/callback?code=ok&state=" + *state_token + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(callback_success.find("200 OK") != std::string::npos);
    REQUIRE(callback_success.find("Authorization complete. You can close this tab.") != std::string::npos);
    REQUIRE(callback_success.find("window.close") != std::string::npos);
    REQUIRE(fake_provider->authorization_exchange_calls() >= 2);

    REQUIRE(runtime.stop());
    tightrope::auth::oauth::clear_provider_client_for_testing();
    tightrope::auth::oauth::reset_oauth_state_for_testing();
    std::filesystem::remove(db_path);
}

TEST_CASE("uwebsockets runtime serves API keys CRUD endpoints", "[server][runtime][admin][api-keys]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const std::string create_body =
        R"({"name":"Primary Key","allowedModels":["gpt-5.4"],"enforcedModel":"gpt-5.4","enforcedReasoningEffort":"high"})";
    const auto created = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/api-keys/ HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(create_body.size()) + "\r\n\r\n" + create_body
    );
    REQUIRE(created.find("201 Created") != std::string::npos);
    const auto created_body = tightrope::tests::server::http_body(created);
    const auto key_id = json_string_field(created_body, "id");
    REQUIRE(key_id.has_value());
    const auto key_secret = json_string_field(created_body, "key");
    REQUIRE(key_secret.has_value());
    REQUIRE(key_secret->rfind("sk-clb-", 0) == 0);

    const auto listed = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/api-keys/ HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(listed.find("200 OK") != std::string::npos);
    REQUIRE(listed.find(*key_id) != std::string::npos);

    const std::string patch_body = R"({"name":"Renamed Key","isActive":false})";
    const auto updated = tightrope::tests::server::send_raw_http(
        port,
        "PATCH /api/api-keys/" + *key_id +
            " HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Content-Type: application/json\r\n"
            "Connection: close\r\n"
            "Content-Length: " +
            std::to_string(patch_body.size()) + "\r\n\r\n" + patch_body
    );
    REQUIRE(updated.find("200 OK") != std::string::npos);
    REQUIRE(updated.find("\"name\":\"Renamed Key\"") != std::string::npos);
    REQUIRE(json_bool_field(tightrope::tests::server::http_body(updated), "isActive", false));

    const auto regenerated = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/api-keys/" + *key_id +
            "/regenerate HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Connection: close\r\n"
            "Content-Length: 0\r\n\r\n"
    );
    REQUIRE(regenerated.find("200 OK") != std::string::npos);
    REQUIRE(regenerated.find("\"key\":\"sk-clb-") != std::string::npos);

    const auto deleted = tightrope::tests::server::send_raw_http(
        port,
        "DELETE /api/api-keys/" + *key_id + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(deleted.find("204 No Content") != std::string::npos);

    const auto final_list = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/api-keys/ HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(final_list.find("200 OK") != std::string::npos);
    REQUIRE(tightrope::tests::server::http_body(final_list).find("[]") != std::string::npos);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("uwebsockets runtime serves account admin endpoints", "[server][runtime][admin][accounts]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const std::string import_body = R"({"email":"test@example.com","provider":"openai"})";
    const auto imported = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/accounts/import HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(import_body.size()) + "\r\n\r\n" + import_body
    );
    REQUIRE(imported.find("201 Created") != std::string::npos);
    const auto account_id = json_string_field(tightrope::tests::server::http_body(imported), "accountId");
    REQUIRE(account_id.has_value());

    const auto listed = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/accounts HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(listed.find("200 OK") != std::string::npos);
    REQUIRE(listed.find(*account_id) != std::string::npos);

    const auto paused = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/accounts/" + *account_id + "/pause HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(paused.find("200 OK") != std::string::npos);
    REQUIRE(paused.find("\"status\":\"paused\"") != std::string::npos);

    const auto reactivated = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/accounts/" + *account_id +
            "/reactivate HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(reactivated.find("200 OK") != std::string::npos);
    REQUIRE(reactivated.find("\"status\":\"reactivated\"") != std::string::npos);

    const auto deleted = tightrope::tests::server::send_raw_http(
        port,
        "DELETE /api/accounts/" + *account_id + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(deleted.find("200 OK") != std::string::npos);
    REQUIRE(deleted.find("\"status\":\"deleted\"") != std::string::npos);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}
