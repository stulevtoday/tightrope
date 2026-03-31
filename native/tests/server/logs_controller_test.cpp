#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include <sqlite3.h>

#include "migration/migration_runner.h"
#include "controllers/logs_controller.h"
#include "repositories/request_log_repo.h"

namespace {

std::string make_temp_db_path() {
    const auto file = std::filesystem::temp_directory_path() / std::filesystem::path("tightrope-logs-controller.sqlite3");
    std::filesystem::remove(file);
    return file.string();
}

} // namespace

TEST_CASE("logs controller lists persisted request logs", "[server][logs]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::db::RequestLogWrite first;
    first.account_id = 1;
    first.path = "/v1/responses";
    first.method = "POST";
    first.status_code = 200;
    first.model = "gpt-5.4";
    first.transport = "http";
    REQUIRE(tightrope::db::append_request_log(db, first));

    tightrope::db::RequestLogWrite second;
    second.account_id = 2;
    second.path = "/backend-api/codex/responses";
    second.method = "POST";
    second.status_code = 101;
    second.error_code = "previous_response_not_found";
    second.transport = "websocket";
    REQUIRE(tightrope::db::append_request_log(db, second));

    const auto all = tightrope::server::controllers::list_request_logs(10, 0, db);
    REQUIRE(all.status == 200);
    REQUIRE(all.logs.size() == 2);
    REQUIRE(all.logs[0].path == "/backend-api/codex/responses");
    REQUIRE(all.logs[0].transport.has_value());
    REQUIRE(*all.logs[0].transport == "websocket");
    REQUIRE(all.logs[1].path == "/v1/responses");

    const auto paged = tightrope::server::controllers::list_request_logs(1, 1, db);
    REQUIRE(paged.status == 200);
    REQUIRE(paged.limit == 1);
    REQUIRE(paged.offset == 1);
    REQUIRE(paged.logs.size() == 1);
    REQUIRE(paged.logs[0].path == "/v1/responses");

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}
