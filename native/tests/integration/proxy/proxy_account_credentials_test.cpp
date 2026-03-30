#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <string>

#include <sqlite3.h>

#include "migration/migration_runner.h"
#include "oauth/token_refresh.h"
#include "repositories/account_repo.h"
#include "controllers/proxy_controller.h"
#include "server/oauth_provider_fake.h"
#include "server/runtime_test_utils.h"
#include "tests/integration/proxy/include/test_support/fake_upstream_transport.h"

namespace {

struct RefreshProviderGuard {
    ~RefreshProviderGuard() {
        tightrope::auth::oauth::clear_token_refresh_provider_for_testing();
    }
};

void seed_account(
    sqlite3* db,
    const std::string& email,
    const std::string& account_id,
    const std::string& access_token
) {
    tightrope::db::OauthAccountUpsert account;
    account.email = email;
    account.provider = "openai";
    account.chatgpt_account_id = account_id;
    account.plan_type = "plus";
    account.access_token_encrypted = access_token;
    account.refresh_token_encrypted = "refresh-" + account_id;
    account.id_token_encrypted = "id-" + account_id;
    REQUIRE(tightrope::db::upsert_oauth_account(db, account).has_value());
}

std::string make_oauth_db_with_accounts() {
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    seed_account(db, "first@example.com", "acc-first", "token-first");
    seed_account(db, "second@example.com", "acc-second", "token-second");

    sqlite3_close(db);
    return db_path;
}

std::string make_oauth_db_with_single_account(
    const std::string& account_id,
    const std::string& access_token,
    const std::string& refresh_token
) {
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::db::OauthAccountUpsert account;
    account.email = "single@example.com";
    account.provider = "openai";
    account.chatgpt_account_id = account_id;
    account.plan_type = "plus";
    account.access_token_encrypted = access_token;
    account.refresh_token_encrypted = refresh_token;
    account.id_token_encrypted = tightrope::tests::server::make_id_token("single@example.com");
    REQUIRE(tightrope::db::upsert_oauth_account(db, account).has_value());

    sqlite3_close(db);
    return db_path;
}

} // namespace

TEST_CASE("responses JSON uses persisted OAuth account token for upstream authorization", "[proxy][auth][credentials]") {
    const auto db_path = make_oauth_db_with_accounts();
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(db_path));

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_credentials","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto response = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"credential-check"})"
    );

    REQUIRE(response.status == 200);
    REQUIRE(fake->call_count == 1);
    REQUIRE(fake->last_plan.headers.find("Authorization") != fake->last_plan.headers.end());
    REQUIRE(fake->last_plan.headers.at("Authorization") == "Bearer token-second");
    REQUIRE(fake->last_plan.headers.find("chatgpt-account-id") != fake->last_plan.headers.end());
    REQUIRE(fake->last_plan.headers.at("chatgpt-account-id") == "acc-second");

    std::filesystem::remove(db_path);
}

TEST_CASE("explicit chatgpt-account-id header selects matching persisted OAuth account token", "[proxy][auth][credentials]") {
    const auto db_path = make_oauth_db_with_accounts();
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(db_path));

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .events = {
                    R"({"type":"response.created","response":{"id":"resp_ws_credentials","status":"in_progress"}})",
                    R"({"type":"response.completed","response":{"id":"resp_ws_credentials","object":"response","status":"completed","output":[]}})",
                },
                .accepted = true,
                .close_code = 1000,
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const tightrope::proxy::openai::HeaderMap inbound = {
        {"chatgpt-account-id", "acc-first"},
        {"Connection", "Upgrade"},
        {"Sec-WebSocket-Key", "abc"},
        {"Originator", "codex_cli_rs"},
    };
    const auto response = tightrope::server::controllers::proxy_responses_websocket(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"credential-check"})",
        inbound
    );

    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(fake->call_count == 1);
    REQUIRE(fake->last_plan.headers.find("Authorization") != fake->last_plan.headers.end());
    REQUIRE(fake->last_plan.headers.at("Authorization") == "Bearer token-first");
    REQUIRE(fake->last_plan.headers.find("chatgpt-account-id") != fake->last_plan.headers.end());
    REQUIRE(fake->last_plan.headers.at("chatgpt-account-id") == "acc-first");

    std::filesystem::remove(db_path);
}

TEST_CASE("transcribe uses persisted OAuth account credentials for upstream calls", "[proxy][auth][credentials]") {
    const auto db_path = make_oauth_db_with_accounts();
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(db_path));

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"text":"transcribed"})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const tightrope::proxy::openai::HeaderMap inbound = {
        {"chatgpt-account-id", "acc-first"},
    };
    const auto response = tightrope::server::controllers::post_proxy_transcribe(
        "/backend-api/transcribe",
        "gpt-4o-transcribe",
        "hello",
        "audio-bytes",
        inbound
    );

    REQUIRE(response.status == 200);
    REQUIRE(fake->call_count == 1);
    REQUIRE(fake->last_plan.headers.find("Authorization") != fake->last_plan.headers.end());
    REQUIRE(fake->last_plan.headers.at("Authorization") == "Bearer token-first");
    REQUIRE(fake->last_plan.headers.find("chatgpt-account-id") != fake->last_plan.headers.end());
    REQUIRE(fake->last_plan.headers.at("chatgpt-account-id") == "acc-first");

    std::filesystem::remove(db_path);
}

TEST_CASE("responses JSON retries once with refreshed OAuth token after upstream 401", "[proxy][auth][refresh]") {
    const auto db_path = make_oauth_db_with_single_account("acc-refresh", "access-token-old", "refresh-token-old");
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(db_path));

    RefreshProviderGuard refresh_provider_guard{};
    auto refresh_provider = std::make_shared<tightrope::tests::server::OAuthProviderFake>("single@example.com");
    tightrope::auth::oauth::set_token_refresh_provider_for_testing(refresh_provider);

    std::size_t call_count = 0;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&call_count](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            ++call_count;
            if (call_count == 1) {
                REQUIRE(plan.headers.at("Authorization") == "Bearer access-token-old");
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 401,
                    .body = R"({"error":{"code":"invalid_api_key","message":"expired","type":"authentication_error"}})",
                    .error_code = "invalid_api_key",
                };
            }

            REQUIRE(plan.headers.at("Authorization") == "Bearer access-token-refreshed");
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_refresh","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto response = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"refresh"})",
        {{"chatgpt-account-id", "acc-refresh"}}
    );

    REQUIRE(response.status == 200);
    REQUIRE(call_count == 2);
    REQUIRE(refresh_provider->refresh_exchange_calls() == 1);

    std::filesystem::remove(db_path);
}
