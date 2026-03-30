#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <string>

#include <sqlite3.h>

#include "migration/migration_runner.h"
#include "controllers/accounts_controller.h"
#include "repositories/account_repo.h"
#include "usage_fetcher.h"

namespace {

std::string make_temp_db_path() {
    const auto file = std::filesystem::temp_directory_path() / std::filesystem::path("tightrope-accounts-controller.sqlite3");
    std::filesystem::remove(file);
    return file.string();
}

class StaticUsagePayloadFetcher final : public tightrope::usage::UsagePayloadFetcher {
  public:
    explicit StaticUsagePayloadFetcher(std::optional<tightrope::usage::UsagePayloadSnapshot> payload)
        : payload_(std::move(payload)) {}

    [[nodiscard]] std::optional<tightrope::usage::UsagePayloadSnapshot> fetch(
        std::string_view access_token,
        std::string_view account_id
    ) override {
        static_cast<void>(access_token);
        static_cast<void>(account_id);
        return payload_;
    }

  private:
    std::optional<tightrope::usage::UsagePayloadSnapshot> payload_;
};

} // namespace

TEST_CASE("accounts controller supports import list pause reactivate delete", "[server][accounts]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    const auto created = tightrope::server::controllers::import_account("test@example.com", "openai", db);
    REQUIRE(created.status == 201);
    REQUIRE(created.account.email == "test@example.com");
    REQUIRE(created.account.provider == "openai");
    REQUIRE(created.account.status == "active");
    REQUIRE_FALSE(created.account.account_id.empty());

    const auto listed = tightrope::server::controllers::list_accounts(db);
    REQUIRE(listed.status == 200);
    REQUIRE(listed.accounts.size() == 1);
    REQUIRE(listed.accounts.front().account_id == created.account.account_id);

    const auto paused = tightrope::server::controllers::pause_account(created.account.account_id, db);
    REQUIRE(paused.status == 200);
    REQUIRE(paused.account.status == "paused");

    const auto reactivated = tightrope::server::controllers::reactivate_account(created.account.account_id, db);
    REQUIRE(reactivated.status == 200);
    REQUIRE(reactivated.account.status == "active");

    const auto removed = tightrope::server::controllers::delete_account(created.account.account_id, db);
    REQUIRE(removed.status == 200);
    REQUIRE(removed.code.empty());

    const auto final_list = tightrope::server::controllers::list_accounts(db);
    REQUIRE(final_list.status == 200);
    REQUIRE(final_list.accounts.empty());

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE("accounts controller refreshes usage telemetry from provider payload", "[server][accounts][usage]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::db::OauthAccountUpsert account;
    account.email = "usage@example.com";
    account.provider = "openai";
    account.chatgpt_account_id = "acc-usage-001";
    account.plan_type = "plus";
    account.access_token_encrypted = "access-token";
    account.refresh_token_encrypted = "refresh-token";
    account.id_token_encrypted = "id-token";
    const auto created = tightrope::db::upsert_oauth_account(db, account);
    REQUIRE(created.has_value());

    tightrope::usage::UsagePayloadSnapshot snapshot;
    snapshot.plan_type = "enterprise";
    snapshot.rate_limit = tightrope::usage::UsageRateLimitDetails{
        .allowed = true,
        .limit_reached = false,
        .primary_window = tightrope::usage::UsageWindowSnapshot{.used_percent = 37},
        .secondary_window = tightrope::usage::UsageWindowSnapshot{.used_percent = 68},
    };

    tightrope::usage::set_usage_payload_fetcher_for_testing(std::make_shared<StaticUsagePayloadFetcher>(snapshot));
    const auto refreshed =
        tightrope::server::controllers::refresh_account_usage(std::to_string(created->id), db);
    tightrope::usage::clear_usage_payload_fetcher_for_testing();

    REQUIRE(refreshed.status == 200);
    REQUIRE(refreshed.account.account_id == std::to_string(created->id));
    REQUIRE(refreshed.account.plan_type.has_value());
    REQUIRE(*refreshed.account.plan_type == "enterprise");
    REQUIRE(refreshed.account.quota_primary_percent.has_value());
    REQUIRE(*refreshed.account.quota_primary_percent == 37);
    REQUIRE(refreshed.account.quota_secondary_percent.has_value());
    REQUIRE(*refreshed.account.quota_secondary_percent == 68);

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}
