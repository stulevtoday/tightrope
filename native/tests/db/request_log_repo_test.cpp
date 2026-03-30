#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>

#include <sqlite3.h>

#include "migration/migration_runner.h"
#include "repositories/account_repo.h"
#include "repositories/request_log_repo.h"

namespace {

std::string make_temp_db_path() {
    const auto file = std::filesystem::temp_directory_path() / std::filesystem::path("tightrope-request-log-repo.sqlite3");
    std::filesystem::remove(file);
    return file.string();
}

} // namespace

TEST_CASE("request log repository persists and reads request lifecycle rows", "[db][request-log]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));
    REQUIRE(tightrope::db::ensure_request_log_schema(db));

    tightrope::db::OauthAccountUpsert account;
    account.email = "request-log@example.com";
    account.provider = "openai";
    account.chatgpt_account_id = "acc-repo-001";
    account.plan_type = "plus";
    account.access_token_encrypted = "enc-access";
    account.refresh_token_encrypted = "enc-refresh";
    account.id_token_encrypted = "enc-id";
    const auto upserted = tightrope::db::upsert_oauth_account(db, account);
    REQUIRE(upserted.has_value());

    const auto resolved_id = tightrope::db::find_account_id_by_chatgpt_account_id(db, "acc-repo-001");
    REQUIRE(resolved_id.has_value());
    REQUIRE(*resolved_id == upserted->id);

    tightrope::db::RequestLogWrite write;
    write.account_id = resolved_id;
    write.path = "/v1/responses";
    write.method = "POST";
    write.status_code = 200;
    write.model = "gpt-5.4";
    write.transport = "http";
    write.total_cost = 0.0;
    REQUIRE(tightrope::db::append_request_log(db, write));

    const auto rows = tightrope::db::list_recent_request_logs(db, 10, 0);
    REQUIRE(rows.size() == 1);
    REQUIRE(rows.front().account_id.has_value());
    REQUIRE(*rows.front().account_id == *resolved_id);
    REQUIRE(rows.front().path == "/v1/responses");
    REQUIRE(rows.front().method == "POST");
    REQUIRE(rows.front().status_code == 200);
    REQUIRE(rows.front().model.has_value());
    REQUIRE(*rows.front().model == "gpt-5.4");
    REQUIRE(rows.front().transport.has_value());
    REQUIRE(*rows.front().transport == "http");
    REQUIRE_FALSE(rows.front().requested_at.empty());

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}
