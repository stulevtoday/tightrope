#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sqlite3.h>

#include "migration/migration_runner.h"
#include "oauth/token_refresh.h"
#include "repositories/account_repo.h"
#include "repositories/request_log_repo.h"
#include "repositories/settings_repo.h"
#include "controllers/proxy_controller.h"
#include "server/oauth_provider_fake.h"
#include "server/runtime_test_utils.h"
#include "tests/integration/proxy/include/test_support/fake_upstream_transport.h"
#include "token_store.h"

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
    {
        tightrope::db::DashboardSettingsPatch patch;
        patch.sticky_threads_enabled = true;
        const auto updated = tightrope::db::update_dashboard_settings(db, patch);
        REQUIRE(updated.has_value());
        REQUIRE(updated->sticky_threads_enabled);
    }

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

std::string query_account_access_token_blob(sqlite3* db, const std::string& account_id) {
    if (db == nullptr || account_id.empty()) {
        return {};
    }
    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql =
        "SELECT access_token_encrypted FROM accounts WHERE chatgpt_account_id = ?1 LIMIT 1;";
    if (sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return {};
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };
    if (sqlite3_bind_text(stmt, 1, account_id.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        finalize();
        return {};
    }
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        finalize();
        return {};
    }
    std::string value;
    if (const auto* raw = sqlite3_column_text(stmt, 0); raw != nullptr) {
        value = reinterpret_cast<const char*>(raw);
    }
    finalize();
    return value;
}

std::string query_account_status(sqlite3* db, const std::string& account_id) {
    if (db == nullptr || account_id.empty()) {
        return {};
    }
    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql = "SELECT status FROM accounts WHERE chatgpt_account_id = ?1 LIMIT 1;";
    if (sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return {};
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };
    if (sqlite3_bind_text(stmt, 1, account_id.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        finalize();
        return {};
    }
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        finalize();
        return {};
    }
    std::string value;
    if (const auto* raw = sqlite3_column_text(stmt, 0); raw != nullptr) {
        value = reinterpret_cast<const char*>(raw);
    }
    finalize();
    return value;
}

bool query_account_routing_pinned(sqlite3* db, const std::string& account_id) {
    if (db == nullptr || account_id.empty()) {
        return false;
    }
    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql = "SELECT routing_pinned FROM accounts WHERE chatgpt_account_id = ?1 LIMIT 1;";
    if (sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return false;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };
    if (sqlite3_bind_text(stmt, 1, account_id.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        finalize();
        return false;
    }
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        finalize();
        return false;
    }
    const auto pinned = sqlite3_column_int(stmt, 0);
    finalize();
    return pinned != 0;
}

std::size_t count_sticky_sessions_for_account(sqlite3* db, const std::string& account_id) {
    if (db == nullptr || account_id.empty()) {
        return 0;
    }
    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql = "SELECT COUNT(1) FROM proxy_sticky_sessions WHERE account_id = ?1;";
    if (sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return 0;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };
    if (sqlite3_bind_text(stmt, 1, account_id.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        finalize();
        return 0;
    }
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        finalize();
        return 0;
    }
    const auto count = sqlite3_column_int64(stmt, 0);
    finalize();
    if (count <= 0) {
        return 0;
    }
    return static_cast<std::size_t>(count);
}

void configure_routing_strategy(sqlite3* db, std::string strategy) {
    tightrope::db::DashboardSettingsPatch patch;
    patch.routing_strategy = std::move(strategy);
    const auto updated = tightrope::db::update_dashboard_settings(db, patch);
    REQUIRE(updated.has_value());
}

void configure_locked_routing_accounts(sqlite3* db, std::string account_ids_csv) {
    tightrope::db::DashboardSettingsPatch patch;
    patch.locked_routing_account_ids = std::move(account_ids_csv);
    const auto updated = tightrope::db::update_dashboard_settings(db, patch);
    REQUIRE(updated.has_value());
}

void configure_affinity_ttl_seconds(sqlite3* db, const std::int64_t seconds) {
    tightrope::db::DashboardSettingsPatch patch;
    patch.openai_cache_affinity_max_age_seconds = seconds;
    const auto updated = tightrope::db::update_dashboard_settings(db, patch);
    REQUIRE(updated.has_value());
}

void configure_routing_success_rate_rho(sqlite3* db, const double rho) {
    tightrope::db::DashboardSettingsPatch patch;
    patch.routing_success_rate_rho = rho;
    const auto updated = tightrope::db::update_dashboard_settings(db, patch);
    REQUIRE(updated.has_value());
}

void insert_request_log_rows(sqlite3* db, const std::int64_t account_id, const int status_code, const int count) {
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::ensure_request_log_schema(db));
    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql = R"SQL(
INSERT INTO request_logs (account_id, path, method, status_code, total_cost)
VALUES (?1, '/backend-api/codex/responses', 'POST', ?2, 0.0);
)SQL";
    REQUIRE(sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(stmt != nullptr);
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };
    for (int i = 0; i < count; ++i) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        REQUIRE(sqlite3_bind_int64(stmt, 1, account_id) == SQLITE_OK);
        REQUIRE(sqlite3_bind_int(stmt, 2, status_code) == SQLITE_OK);
        REQUIRE(sqlite3_step(stmt) == SQLITE_DONE);
    }
    finalize();
}

std::optional<std::int64_t> query_sticky_session_ttl_ms(sqlite3* db, const std::string& session_key) {
    if (db == nullptr || session_key.empty()) {
        return std::nullopt;
    }
    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql =
        "SELECT expires_at_ms - updated_at_ms FROM proxy_sticky_sessions WHERE session_key = ?1 LIMIT 1;";
    if (sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return std::nullopt;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };
    if (sqlite3_bind_text(stmt, 1, session_key.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        finalize();
        return std::nullopt;
    }
    if (sqlite3_step(stmt) != SQLITE_ROW || sqlite3_column_type(stmt, 0) == SQLITE_NULL) {
        finalize();
        return std::nullopt;
    }
    const auto ttl_ms = sqlite3_column_int64(stmt, 0);
    finalize();
    return ttl_ms;
}

void set_quota_usage(
    sqlite3* db,
    const std::int64_t account_id,
    const std::optional<int> quota_primary_percent,
    const std::optional<int> quota_secondary_percent
) {
    REQUIRE(tightrope::db::update_account_usage_telemetry(db, account_id, quota_primary_percent, quota_secondary_percent));
}

void set_quota_usage_with_reset(
    sqlite3* db,
    const std::int64_t account_id,
    const std::optional<int> quota_primary_percent,
    const std::optional<int> quota_secondary_percent,
    const std::optional<std::int64_t> quota_primary_reset_at_ms,
    const std::optional<std::int64_t> quota_secondary_reset_at_ms
) {
    REQUIRE(tightrope::db::update_account_usage_telemetry(
        db,
        account_id,
        quota_primary_percent,
        quota_secondary_percent,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        quota_primary_reset_at_ms,
        quota_secondary_reset_at_ms));
}

void set_account_status(sqlite3* db, const std::string& account_id, const std::string& status) {
    REQUIRE(db != nullptr);
    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql =
        "UPDATE accounts SET status = ?1, updated_at = datetime('now') WHERE chatgpt_account_id = ?2;";
    REQUIRE(sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(stmt != nullptr);
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };
    REQUIRE(sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);
    REQUIRE(sqlite3_bind_text(stmt, 2, account_id.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_DONE);
    REQUIRE(sqlite3_changes(db) == 1);
    finalize();
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
    "responses JSON migrates legacy plaintext access token on read when key is configured",
    "[proxy][auth][credentials][crypto]"
) {
    tightrope::tests::server::EnvVarGuard key_hex_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_HEX"};
    tightrope::tests::server::EnvVarGuard key_file_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE"};
    tightrope::tests::server::EnvVarGuard key_passphrase_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE_PASSPHRASE"};
    tightrope::tests::server::EnvVarGuard strict_mode_guard{"TIGHTROPE_TOKEN_ENCRYPTION_REQUIRE_ENCRYPTED_AT_REST"};
    tightrope::tests::server::EnvVarGuard migrate_guard{"TIGHTROPE_TOKEN_ENCRYPTION_MIGRATE_PLAINTEXT_ON_READ"};
    REQUIRE(key_file_guard.set(""));
    REQUIRE(key_passphrase_guard.set(""));
    REQUIRE(strict_mode_guard.set("0"));
    REQUIRE(migrate_guard.set("1"));
    REQUIRE(key_hex_guard.set(""));
    tightrope::auth::crypto::reset_token_storage_crypto_for_testing();

    const auto db_path = make_oauth_db_with_accounts();
    {
        sqlite3* db = open_db_readwrite(db_path);
        REQUIRE(query_account_access_token_blob(db, "acc-second") == "token-second");
        sqlite3_close(db);
    }

    REQUIRE(
        key_hex_guard.set("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff")
    );
    REQUIRE(strict_mode_guard.set("1"));
    REQUIRE(migrate_guard.set("1"));
    tightrope::auth::crypto::reset_token_storage_crypto_for_testing();

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(db_path));

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_credentials_migrated","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto response = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"credential-migration-check"})"
    );

    REQUIRE(response.status == 200);
    REQUIRE(fake->call_count == 1);
    REQUIRE(fake->last_plan.headers.at("Authorization") == "Bearer token-second");
    REQUIRE(fake->last_plan.headers.at("chatgpt-account-id") == "acc-second");

    {
        sqlite3* db = open_db_readwrite(db_path);
        const auto stored_after = query_account_access_token_blob(db, "acc-second");
        REQUIRE_FALSE(stored_after.empty());
        REQUIRE(stored_after != "token-second");
        REQUIRE(stored_after.starts_with(tightrope::auth::crypto::token_storage_cipher_prefix()));
        sqlite3_close(db);
    }

    std::filesystem::remove(db_path);
}

TEST_CASE(
    "responses JSON decrypts encrypted-at-rest OAuth account token for upstream authorization",
    "[proxy][auth][credentials][crypto]"
) {
    tightrope::tests::server::EnvVarGuard key_hex_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_HEX"};
    tightrope::tests::server::EnvVarGuard key_file_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE"};
    tightrope::tests::server::EnvVarGuard key_passphrase_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE_PASSPHRASE"};
    REQUIRE(key_file_guard.set(""));
    REQUIRE(key_passphrase_guard.set(""));
    REQUIRE(
        key_hex_guard.set("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff")
    );
    tightrope::auth::crypto::reset_token_storage_crypto_for_testing();

    const auto db_path = make_oauth_db_with_accounts();
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(db_path));

    {
        sqlite3* db = open_db_readwrite(db_path);
        const auto stored_token = query_account_access_token_blob(db, "acc-second");
        REQUIRE_FALSE(stored_token.empty());
        REQUIRE(stored_token != "token-second");
        REQUIRE(stored_token.starts_with(tightrope::auth::crypto::token_storage_cipher_prefix()));
        sqlite3_close(db);
    }

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_credentials_encrypted","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto response = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"credential-check-encrypted"})"
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
        configure_routing_strategy(db, "weighted_round_robin");
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
    "responses JSON drain-hop strategy keeps routing on the same active account",
    "[proxy][auth][credentials][routing][drain-hop]"
) {
    const auto seeded = make_oauth_db_with_two_accounts();
    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        configure_routing_strategy(db, "drain_hop");
        set_quota_usage(db, seeded.first_internal_id, 40, 40);
        set_quota_usage(db, seeded.second_internal_id, 45, 45);
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
                .body = R"({"id":"resp_drain_hop_sticky","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto first = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"drain-hop-sticky-1"})"
    );
    const auto second = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"drain-hop-sticky-2"})"
    );
    const auto third = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"drain-hop-sticky-3"})"
    );

    REQUIRE(first.status == 200);
    REQUIRE(second.status == 200);
    REQUIRE(third.status == 200);
    REQUIRE(observed_plans.size() == 3);
    const auto selected_account = observed_plans[0].headers.at("chatgpt-account-id");
    REQUIRE(observed_plans[1].headers.at("chatgpt-account-id") == selected_account);
    REQUIRE(observed_plans[2].headers.at("chatgpt-account-id") == selected_account);

    std::filesystem::remove(seeded.db_path);
}

TEST_CASE(
    "responses JSON drain-hop strategy hops when current account becomes unavailable",
    "[proxy][auth][credentials][routing][drain-hop]"
) {
    const auto seeded = make_oauth_db_with_two_accounts();
    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        configure_routing_strategy(db, "drain_hop");
        set_quota_usage(db, seeded.first_internal_id, 20, 20);
        set_quota_usage(db, seeded.second_internal_id, 20, 20);
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
                .body = R"({"id":"resp_drain_hop_hop","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto first = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"drain-hop-hop-1"})"
    );
    REQUIRE(first.status == 200);
    REQUIRE(observed_plans.size() == 1);
    const auto first_account = observed_plans[0].headers.at("chatgpt-account-id");

    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        set_account_status(db, first_account, "rate_limited");
        sqlite3_close(db);
    }

    const auto second = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"drain-hop-hop-2"})"
    );
    REQUIRE(second.status == 200);
    REQUIRE(observed_plans.size() == 2);
    REQUIRE(observed_plans[1].headers.at("chatgpt-account-id") != first_account);

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

TEST_CASE(
    "responses JSON weighted strategy respects routing_success_rate_rho from settings",
    "[proxy][auth][credentials][routing][success-rate-rho]"
) {
    const auto seeded = make_oauth_db_with_two_accounts();
    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        configure_routing_strategy(db, "weighted_round_robin");
        set_quota_usage(db, seeded.first_internal_id, 50, 50);
        set_quota_usage(db, seeded.second_internal_id, 50, 50);
        configure_routing_success_rate_rho(db, 8.0);
        insert_request_log_rows(db, seeded.first_internal_id, 200, 12);
        insert_request_log_rows(db, seeded.second_internal_id, 500, 12);
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
                .body = R"({"id":"resp_success_rate_rho","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    for (int i = 0; i < 3; ++i) {
        const auto response = tightrope::server::controllers::post_proxy_responses_json(
            "/v1/responses",
            R"({"model":"gpt-5.4","input":"success-rate-rho-check"})"
        );
        REQUIRE(response.status == 200);
    }

    REQUIRE(observed_plans.size() == 3);
    for (const auto& plan : observed_plans) {
        REQUIRE(plan.headers.at("chatgpt-account-id") == "acc-first");
        REQUIRE(plan.headers.at("Authorization") == "Bearer token-first");
    }

    std::filesystem::remove(seeded.db_path);
}

TEST_CASE(
    "responses JSON prefer-earlier-reset setting biases round-robin routing order",
    "[proxy][auth][credentials][routing][prefer-reset]"
) {
    const auto seeded = make_oauth_db_with_two_accounts();
    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        configure_routing_strategy(db, "round_robin");
        set_quota_usage_with_reset(db, seeded.first_internal_id, 50, 50, 1'000'000, 1'000'000);
        set_quota_usage_with_reset(db, seeded.second_internal_id, 50, 50, 2'000'000, 2'000'000);
        tightrope::db::DashboardSettingsPatch patch;
        patch.prefer_earlier_reset_accounts = true;
        const auto updated = tightrope::db::update_dashboard_settings(db, patch);
        REQUIRE(updated.has_value());
        REQUIRE(updated->prefer_earlier_reset_accounts);
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
                .body = R"({"id":"resp_prefer_reset","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto response = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"prefer-reset-check"})"
    );

    REQUIRE(response.status == 200);
    REQUIRE(observed_plans.size() == 1);
    REQUIRE(observed_plans.front().headers.at("chatgpt-account-id") == "acc-first");
    REQUIRE(observed_plans.front().headers.at("Authorization") == "Bearer token-first");

    std::filesystem::remove(seeded.db_path);
}

TEST_CASE(
    "responses JSON pin overrides routing strategy and explicit account preference",
    "[proxy][auth][credentials][routing][pin]"
) {
    const auto seeded = make_oauth_db_with_two_accounts();
    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        configure_routing_strategy(db, "weighted_round_robin");
        set_quota_usage(db, seeded.first_internal_id, 95, 95);
        set_quota_usage(db, seeded.second_internal_id, 5, 5);
        REQUIRE(tightrope::db::set_account_routing_pinned(db, seeded.first_internal_id, true));
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
                .body = R"({"id":"resp_pinned","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto first = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"pin-routing-check"})"
    );
    const auto second = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"pin-header-override-check"})",
        {{"chatgpt-account-id", "acc-second"}}
    );

    REQUIRE(first.status == 200);
    REQUIRE(second.status == 200);
    REQUIRE(observed_plans.size() == 2);
    REQUIRE(observed_plans[0].headers.at("chatgpt-account-id") == "acc-first");
    REQUIRE(observed_plans[1].headers.at("chatgpt-account-id") == "acc-first");
    REQUIRE(observed_plans[0].headers.at("Authorization") == "Bearer token-first");
    REQUIRE(observed_plans[1].headers.at("Authorization") == "Bearer token-first");

    std::filesystem::remove(seeded.db_path);
}

TEST_CASE(
    "responses JSON routes inside locked pool and ignores non-pool preferred account",
    "[proxy][auth][credentials][routing][lock-pool]"
) {
    tightrope::tests::server::EnvVarGuard key_hex_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_HEX"};
    tightrope::tests::server::EnvVarGuard key_file_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE"};
    tightrope::tests::server::EnvVarGuard key_passphrase_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE_PASSPHRASE"};
    tightrope::tests::server::EnvVarGuard strict_mode_guard{"TIGHTROPE_TOKEN_ENCRYPTION_REQUIRE_ENCRYPTED_AT_REST"};
    REQUIRE(key_hex_guard.set(""));
    REQUIRE(key_file_guard.set(""));
    REQUIRE(key_passphrase_guard.set(""));
    REQUIRE(strict_mode_guard.set("0"));
    tightrope::auth::crypto::reset_token_storage_crypto_for_testing();

    const auto seeded = make_oauth_db_with_two_accounts();
    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        configure_routing_strategy(db, "weighted_round_robin");
        set_quota_usage(db, seeded.first_internal_id, 90, 90);
        set_quota_usage(db, seeded.second_internal_id, 5, 5);
        configure_locked_routing_accounts(db, "acc-first,acc-missing");
        sqlite3_close(db);
    }

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(seeded.db_path));

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_lock_pool","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto response = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"lock-pool-routing-check"})",
        {{"chatgpt-account-id", "acc-second"}}
    );

    REQUIRE(response.status == 200);
    REQUIRE(fake->call_count == 1);
    REQUIRE(fake->last_plan.headers.at("chatgpt-account-id") == "acc-first");
    REQUIRE(fake->last_plan.headers.at("Authorization") == "Bearer token-first");

    std::filesystem::remove(seeded.db_path);
}

TEST_CASE(
    "responses JSON locked pool overrides pinned account outside pool",
    "[proxy][auth][credentials][routing][lock-pool][pin]"
) {
    tightrope::tests::server::EnvVarGuard key_hex_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_HEX"};
    tightrope::tests::server::EnvVarGuard key_file_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE"};
    tightrope::tests::server::EnvVarGuard key_passphrase_guard{"TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE_PASSPHRASE"};
    tightrope::tests::server::EnvVarGuard strict_mode_guard{"TIGHTROPE_TOKEN_ENCRYPTION_REQUIRE_ENCRYPTED_AT_REST"};
    REQUIRE(key_hex_guard.set(""));
    REQUIRE(key_file_guard.set(""));
    REQUIRE(key_passphrase_guard.set(""));
    REQUIRE(strict_mode_guard.set("0"));
    tightrope::auth::crypto::reset_token_storage_crypto_for_testing();

    const auto seeded = make_oauth_db_with_two_accounts();
    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        configure_routing_strategy(db, "weighted_round_robin");
        set_quota_usage(db, seeded.first_internal_id, 90, 90);
        set_quota_usage(db, seeded.second_internal_id, 5, 5);
        REQUIRE(tightrope::db::set_account_routing_pinned(db, seeded.second_internal_id, true));
        configure_locked_routing_accounts(db, "acc-first,acc-missing");
        REQUIRE_FALSE(query_account_routing_pinned(db, "acc-second"));
        sqlite3_close(db);
    }

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(seeded.db_path));

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_lock_pool_pin_override","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto response = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"lock-pool-overrides-pin"})"
    );

    REQUIRE(response.status == 200);
    REQUIRE(fake->call_count == 1);
    REQUIRE(fake->last_plan.headers.at("chatgpt-account-id") == "acc-first");
    REQUIRE(fake->last_plan.headers.at("Authorization") == "Bearer token-first");

    std::filesystem::remove(seeded.db_path);
}

TEST_CASE(
    "responses JSON routes inside locked pool when settings use internal account ids",
    "[proxy][auth][credentials][routing][lock-pool][internal-id]"
) {
    const auto seeded = make_oauth_db_with_two_accounts();
    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        configure_routing_strategy(db, "weighted_round_robin");
        set_quota_usage(db, seeded.first_internal_id, 90, 90);
        set_quota_usage(db, seeded.second_internal_id, 5, 5);
        configure_locked_routing_accounts(
            db,
            std::to_string(seeded.first_internal_id) + ",999999"
        );
        sqlite3_close(db);
    }

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(seeded.db_path));

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_lock_pool_internal_id","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto response = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"lock-pool-internal-id-check"})",
        {{"chatgpt-account-id", "acc-second"}}
    );

    REQUIRE(response.status == 200);
    REQUIRE(fake->call_count == 1);
    REQUIRE(fake->last_plan.headers.at("chatgpt-account-id") == "acc-first");
    REQUIRE(fake->last_plan.headers.at("Authorization") == "Bearer token-first");

    std::filesystem::remove(seeded.db_path);
}

TEST_CASE(
    "responses JSON continuation honors pinned account when no session continuity key is replayed",
    "[proxy][auth][credentials][routing][pin][continuity]"
) {
    const auto seeded = make_oauth_db_with_two_accounts();
    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        configure_routing_strategy(db, "weighted_round_robin");
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
            const bool first_call = observed_plans.size() == 1;
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = first_call
                            ? R"({"id":"resp_cont_prev","object":"response","status":"completed","output":[]})"
                            : R"({"id":"resp_cont_next","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto first = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"continuity-seed"})",
        {{"chatgpt-account-id", "acc-second"}}
    );
    REQUIRE(first.status == 200);
    REQUIRE(observed_plans.size() == 1);
    REQUIRE(observed_plans[0].headers.at("chatgpt-account-id") == "acc-second");
    REQUIRE(observed_plans[0].headers.at("Authorization") == "Bearer token-second");

    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        REQUIRE(tightrope::db::set_account_routing_pinned(db, seeded.first_internal_id, true));
        sqlite3_close(db);
    }

    const auto second = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","previous_response_id":"resp_cont_prev","input":"continuity-follow-up"})"
    );
    REQUIRE(second.status == 200);
    REQUIRE(observed_plans.size() == 2);
    REQUIRE(observed_plans[1].headers.at("chatgpt-account-id") == "acc-first");
    REQUIRE(observed_plans[1].headers.at("Authorization") == "Bearer token-first");

    std::filesystem::remove(seeded.db_path);
}

TEST_CASE("explicit chatgpt-account-id header selects matching persisted OAuth account token", "[proxy][auth][credentials]") {
    const auto seeded = make_oauth_db_with_two_accounts();
    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        configure_routing_strategy(db, "weighted_round_robin");
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
        configure_routing_strategy(db, "weighted_round_robin");
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

TEST_CASE(
    "sticky affinity reuses account when sticky key is provided via session_id header",
    "[proxy][auth][credentials][routing][sticky]"
) {
    const auto seeded = make_oauth_db_with_two_accounts();
    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        configure_routing_strategy(db, "weighted_round_robin");
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
                .body = R"({"id":"resp_sticky_session_header","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload = R"({"model":"gpt-5.4","input":"sticky-session-id-header"})";
    const auto first = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        payload,
        {{"chatgpt-account-id", "acc-first"}, {"session_id", "sticky-session-header-key"}}
    );
    const auto second = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        payload,
        {{"session_id", "sticky-session-header-key"}}
    );

    REQUIRE(first.status == 200);
    REQUIRE(second.status == 200);
    REQUIRE(observed_plans.size() == 2);
    REQUIRE(observed_plans[0].headers.at("chatgpt-account-id") == "acc-first");
    REQUIRE(observed_plans[0].headers.at("Authorization") == "Bearer token-first");
    REQUIRE(observed_plans[1].headers.at("chatgpt-account-id") == "acc-first");
    REQUIRE(observed_plans[1].headers.at("Authorization") == "Bearer token-first");

    std::filesystem::remove(seeded.db_path);
}

TEST_CASE(
    "sticky affinity TTL follows dashboard openai cache affinity max age setting",
    "[proxy][auth][credentials][routing][sticky]"
) {
    const auto seeded = make_oauth_db_with_two_accounts();
    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        configure_affinity_ttl_seconds(db, 42);
        sqlite3_close(db);
    }

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(seeded.db_path));

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_sticky_ttl","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto response = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"sticky-ttl-check","prompt_cache_key":"sticky-ttl-key"})",
        {{"chatgpt-account-id", "acc-first"}}
    );
    REQUIRE(response.status == 200);

    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        const auto ttl_ms = query_sticky_session_ttl_ms(db, "sticky-ttl-key");
        REQUIRE(ttl_ms.has_value());
        REQUIRE(*ttl_ms == 42'000);
        sqlite3_close(db);
    }

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

TEST_CASE(
    "responses JSON marks deactivated 401 accounts unusable and purges sticky affinity",
    "[proxy][auth][credentials][deactivated]"
) {
    const auto seeded = make_oauth_db_with_two_accounts();
    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        configure_routing_strategy(db, "weighted_round_robin");
        set_quota_usage(db, seeded.first_internal_id, 95, 95);
        set_quota_usage(db, seeded.second_internal_id, 5, 5);
        REQUIRE(tightrope::db::set_account_routing_pinned(db, seeded.first_internal_id, true));
        sqlite3_close(db);
    }

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(seeded.db_path));

    std::size_t call_count = 0;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&call_count](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            ++call_count;
            if (call_count == 1) {
                REQUIRE(plan.headers.at("chatgpt-account-id") == "acc-first");
                REQUIRE(plan.headers.at("Authorization") == "Bearer token-first");
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 401,
                    .body = R"({"error":{"code":"invalid_api_key","message":"Your account has been deactivated","type":"authentication_error"}})",
                };
            }

            REQUIRE(plan.headers.at("chatgpt-account-id") == "acc-second");
            REQUIRE(plan.headers.at("Authorization") == "Bearer token-second");
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_recovered_after_deactivate","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":"deactivated-account-check","prompt_cache_key":"deactivated-sticky-key"})";
    const auto first = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        payload,
        {{"chatgpt-account-id", "acc-first"}}
    );
    REQUIRE(first.status == 401);
    REQUIRE(call_count == 1);

    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        REQUIRE(query_account_status(db, "acc-first") == "deactivated");
        REQUIRE_FALSE(query_account_routing_pinned(db, "acc-first"));
        REQUIRE(count_sticky_sessions_for_account(db, "acc-first") == 0);
        sqlite3_close(db);
    }

    const auto second = tightrope::server::controllers::post_proxy_responses_json("/v1/responses", payload);
    REQUIRE(second.status == 200);
    REQUIRE(call_count == 2);

    std::filesystem::remove(seeded.db_path);
}

TEST_CASE(
    "responses JSON marks exhausted 429 accounts quota blocked, clears pin/sticky affinity, and fails over",
    "[proxy][auth][credentials][exhausted]"
) {
    const auto seeded = make_oauth_db_with_two_accounts();
    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        configure_routing_strategy(db, "weighted_round_robin");
        set_quota_usage(db, seeded.first_internal_id, 95, 95);
        set_quota_usage(db, seeded.second_internal_id, 5, 5);
        REQUIRE(tightrope::db::set_account_routing_pinned(db, seeded.first_internal_id, true));
        sqlite3_close(db);
    }

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(seeded.db_path));

    std::size_t call_count = 0;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&call_count](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            ++call_count;
            if (call_count == 1) {
                REQUIRE(plan.headers.at("chatgpt-account-id") == "acc-first");
                REQUIRE(plan.headers.at("Authorization") == "Bearer token-first");
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 429,
                    .body = R"({"error":{"code":"rate_limit_exceeded","message":"Usage limit has been reached","type":"invalid_request_error"}})",
                };
            }

            REQUIRE(plan.headers.at("chatgpt-account-id") == "acc-second");
            REQUIRE(plan.headers.at("Authorization") == "Bearer token-second");
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_recovered_after_exhaustion","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":"exhausted-account-check","prompt_cache_key":"exhausted-sticky-key"})";
    const auto first = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        payload,
        {{"chatgpt-account-id", "acc-first"}}
    );
    REQUIRE(first.status == 429);
    REQUIRE(call_count == 1);

    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        REQUIRE(query_account_status(db, "acc-first") == "quota_blocked");
        REQUIRE_FALSE(query_account_routing_pinned(db, "acc-first"));
        REQUIRE(count_sticky_sessions_for_account(db, "acc-first") == 0);
        sqlite3_close(db);
    }

    const auto second = tightrope::server::controllers::post_proxy_responses_json("/v1/responses", payload);
    REQUIRE(second.status == 200);
    REQUIRE(call_count == 2);

    std::filesystem::remove(seeded.db_path);
}

TEST_CASE(
    "responses JSON treats upstream_unavailable 500 with quota markers as exhausted and fails over on the next request",
    "[proxy][auth][credentials][exhausted][upstream-unavailable]"
) {
    const auto seeded = make_oauth_db_with_two_accounts();
    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        configure_routing_strategy(db, "weighted_round_robin");
        set_quota_usage(db, seeded.first_internal_id, 50, 50);
        set_quota_usage(db, seeded.second_internal_id, 10, 10);
        REQUIRE(tightrope::db::set_account_routing_pinned(db, seeded.first_internal_id, true));
        sqlite3_close(db);
    }

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(seeded.db_path));

    std::size_t call_count = 0;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&call_count](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            ++call_count;
            if (call_count == 1) {
                REQUIRE(plan.headers.at("chatgpt-account-id") == "acc-first");
                REQUIRE(plan.headers.at("Authorization") == "Bearer token-first");
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 500,
                    .body = "account usage limit reached",
                    .events = {},
                    .headers = {{"x-openai-error-code", "insufficient_quota"}},
                    .accepted = false,
                    .close_code = 1011,
                    .error_code = "upstream_unavailable",
                };
            }

            REQUIRE(plan.headers.at("chatgpt-account-id") == "acc-second");
            REQUIRE(plan.headers.at("Authorization") == "Bearer token-second");
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_recovered_after_upstream_unavailable_exhaustion","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":"upstream-unavailable-exhaustion-check","prompt_cache_key":"upstream-unavailable-exhaustion-sticky-key"})";
    const auto first = tightrope::server::controllers::post_proxy_responses_json("/v1/responses", payload);
    REQUIRE(first.status == 500);
    REQUIRE(call_count == 1);

    {
        sqlite3* db = open_db_readwrite(seeded.db_path);
        REQUIRE(query_account_status(db, "acc-first") == "quota_blocked");
        REQUIRE_FALSE(query_account_routing_pinned(db, "acc-first"));
        REQUIRE(count_sticky_sessions_for_account(db, "acc-first") == 0);
        sqlite3_close(db);
    }

    const auto second = tightrope::server::controllers::post_proxy_responses_json("/v1/responses", payload);
    REQUIRE(second.status == 200);
    REQUIRE(call_count == 2);

    std::filesystem::remove(seeded.db_path);
}
