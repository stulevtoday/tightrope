#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

#include <sqlite3.h>

#include "migration/migration_runner.h"
#include "oauth/token_refresh.h"
#include "repositories/account_repo.h"
#include "server/oauth_provider_fake.h"

namespace {

std::string make_temp_db_path() {
    static std::uint32_t sequence = 0;
    const auto file = std::filesystem::temp_directory_path() /
                      std::filesystem::path("tightrope-oauth-refresh-" + std::to_string(++sequence) + ".sqlite3");
    std::filesystem::remove(file);
    return file.string();
}

std::string query_text_column(sqlite3* db, const std::string_view sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, std::string(sql).c_str(), -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
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

} // namespace

TEST_CASE("token refresh updates persisted OAuth access token for active account", "[auth][oauth][refresh]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::db::OauthAccountUpsert account;
    account.email = "refresh@example.com";
    account.provider = "openai";
    account.chatgpt_account_id = "acc-refresh-1";
    account.plan_type = "plus";
    account.access_token_encrypted = "access-token-old";
    account.refresh_token_encrypted = "refresh-token-old";
    account.id_token_encrypted = tightrope::tests::server::make_id_token("refresh@example.com");
    REQUIRE(tightrope::db::upsert_oauth_account(db, account).has_value());

    auto provider = std::make_shared<tightrope::tests::server::OAuthProviderFake>("refresh@example.com");
    const auto refreshed =
        tightrope::auth::oauth::refresh_access_token_for_account(db, "acc-refresh-1", provider);
    REQUIRE(refreshed.refreshed);
    REQUIRE(refreshed.error_code.empty());

    REQUIRE(
        query_text_column(db, "SELECT access_token_encrypted FROM accounts WHERE chatgpt_account_id = 'acc-refresh-1'")
        == "access-token-refreshed");
    REQUIRE(
        query_text_column(db, "SELECT refresh_token_encrypted FROM accounts WHERE chatgpt_account_id = 'acc-refresh-1'")
        == "refresh-token-old");
    REQUIRE(provider->refresh_exchange_calls() == 1);

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE("token refresh returns account_not_found for missing active account", "[auth][oauth][refresh]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    auto provider = std::make_shared<tightrope::tests::server::OAuthProviderFake>("refresh@example.com");
    const auto refreshed =
        tightrope::auth::oauth::refresh_access_token_for_account(db, "acc-missing", provider);
    REQUIRE_FALSE(refreshed.refreshed);
    REQUIRE(refreshed.error_code == "account_not_found");
    REQUIRE(provider->refresh_exchange_calls() == 0);

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}
