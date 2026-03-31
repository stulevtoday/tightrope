#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "migration/migration_runner.h"
#include "oauth/token_refresh.h"
#include "repositories/account_repo.h"
#include "repositories/settings_repo.h"
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

std::int64_t seed_account(
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
    const auto created = tightrope::db::upsert_oauth_account(db, account);
    REQUIRE(created.has_value());
    return created->id;
}

struct SeededAccounts {
    std::string db_path;
    std::int64_t first_internal_id = 0;
    std::int64_t second_internal_id = 0;
};

SeededAccounts make_oauth_db_with_two_accounts() {
    SeededAccounts seeded;
    seeded.db_path = tightrope::tests::server::make_temp_runtime_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(seeded.db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    seeded.first_internal_id = seed_account(db, "first@example.com", "acc-first", "token-first");
    seeded.second_internal_id = seed_account(db, "second@example.com", "acc-second", "token-second");

    sqlite3_close(db);
    return seeded;
}

std::string make_oauth_db_with_accounts() {
    const auto seeded = make_oauth_db_with_two_accounts();
    return seeded.db_path;
}

sqlite3* open_db_readwrite(const std::string& db_path) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    return db;
}

void configure_routing_strategy(sqlite3* db, std::string strategy) {
    tightrope::db::DashboardSettingsPatch patch;
    patch.routing_strategy = std::move(strategy);
    const auto updated = tightrope::db::update_dashboard_settings(db, patch);
    REQUIRE(updated.has_value());
}

void set_quota_usage(
    sqlite3* db,
    const std::int64_t account_id,
    const std::optional<int> quota_primary_percent,
    const std::optional<int> quota_secondary_percent
) {
    REQUIRE(tightrope::db::update_account_usage_telemetry(db, account_id, quota_primary_percent, quota_secondary_percent));
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

TEST_CASE(
    "responses JSON routes account selection from usage-weighted strategy when no account is pinned",
    "[proxy][auth][credentials][routing]"
) {
    const auto seeded = make_oauth_db_with_two_accounts();
    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        configure_routing_strategy(db, "usage_weighted");
        set_quota_usage(db, seeded.first_internal_id, 90, 90);
        set_quota_usage(db, seeded.second_internal_id, 10, 10);
        sqlite3_close(db);
    }

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(seeded.db_path));

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_usage_weighted","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto response = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"usage-weighted-check"})"
    );

    REQUIRE(response.status == 200);
    REQUIRE(fake->call_count == 1);
    REQUIRE(fake->last_plan.headers.find("Authorization") != fake->last_plan.headers.end());
    REQUIRE(fake->last_plan.headers.at("Authorization") == "Bearer token-second");
    REQUIRE(fake->last_plan.headers.find("chatgpt-account-id") != fake->last_plan.headers.end());
    REQUIRE(fake->last_plan.headers.at("chatgpt-account-id") == "acc-second");

    std::filesystem::remove(seeded.db_path);
}

TEST_CASE(
    "responses JSON round-robin strategy rotates account selection between eligible accounts",
    "[proxy][auth][credentials][routing]"
) {
    const auto seeded = make_oauth_db_with_two_accounts();
    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        configure_routing_strategy(db, "round_robin");
        set_quota_usage(db, seeded.first_internal_id, 50, 50);
        set_quota_usage(db, seeded.second_internal_id, 50, 50);
        sqlite3_close(db);
    }

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(seeded.db_path));

    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_round_robin","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto first = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"round-robin-check-1"})"
    );
    const auto second = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"round-robin-check-2"})"
    );

    REQUIRE(first.status == 200);
    REQUIRE(second.status == 200);
    REQUIRE(observed_plans.size() == 2);
    REQUIRE(observed_plans[0].headers.at("chatgpt-account-id") == "acc-second");
    REQUIRE(observed_plans[0].headers.at("Authorization") == "Bearer token-second");
    REQUIRE(observed_plans[1].headers.at("chatgpt-account-id") == "acc-first");
    REQUIRE(observed_plans[1].headers.at("Authorization") == "Bearer token-first");

    std::filesystem::remove(seeded.db_path);
}

TEST_CASE("explicit chatgpt-account-id header selects matching persisted OAuth account token", "[proxy][auth][credentials]") {
    const auto seeded = make_oauth_db_with_two_accounts();
    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        configure_routing_strategy(db, "usage_weighted");
        set_quota_usage(db, seeded.first_internal_id, 95, 95);
        set_quota_usage(db, seeded.second_internal_id, 5, 5);
        sqlite3_close(db);
    }

    const auto& db_path = seeded.db_path;
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

TEST_CASE(
    "sticky affinity account selection takes precedence over routing strategy defaults",
    "[proxy][auth][credentials][routing][sticky]"
) {
    const auto seeded = make_oauth_db_with_two_accounts();
    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        configure_routing_strategy(db, "usage_weighted");
        set_quota_usage(db, seeded.first_internal_id, 95, 95);
        set_quota_usage(db, seeded.second_internal_id, 5, 5);
        sqlite3_close(db);
    }

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(seeded.db_path));

    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_sticky_routing","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":"sticky-routing-check","prompt_cache_key":"sticky-routing-key"})";
    const auto first = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        payload,
        {{"chatgpt-account-id", "acc-first"}}
    );
    const auto second = tightrope::server::controllers::post_proxy_responses_json("/v1/responses", payload);

    REQUIRE(first.status == 200);
    REQUIRE(second.status == 200);
    REQUIRE(observed_plans.size() == 2);
    REQUIRE(observed_plans[0].headers.at("chatgpt-account-id") == "acc-first");
    REQUIRE(observed_plans[0].headers.at("Authorization") == "Bearer token-first");
    REQUIRE(observed_plans[1].headers.at("chatgpt-account-id") == "acc-first");
    REQUIRE(observed_plans[1].headers.at("Authorization") == "Bearer token-first");

    std::filesystem::remove(seeded.db_path);
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
