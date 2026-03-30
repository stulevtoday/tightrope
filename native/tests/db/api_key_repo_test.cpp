#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "migration/migration_runner.h"
#include "repositories/api_key_repo.h"

namespace {

std::string make_temp_db_path() {
    const auto file = std::filesystem::temp_directory_path() / std::filesystem::path("tightrope-api-key-repo.sqlite3");
    std::filesystem::remove(file);
    return file.string();
}

} // namespace

TEST_CASE("api key repository stores limits and reservation lifecycle", "[db][api-keys]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));
    REQUIRE(tightrope::db::ensure_api_key_schema(db));

    const auto created = tightrope::db::create_api_key(
        db,
        "key_1",
        "hash_1",
        "prefix_1",
        "Key One",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt
    );
    REQUIRE(created.has_value());
    REQUIRE(created->key_id == "key_1");

    std::vector<tightrope::db::ApiKeyLimitInput> limits;
    limits.push_back(
        tightrope::db::ApiKeyLimitInput{
            .limit_type = "total_tokens",
            .limit_window = "weekly",
            .max_value = 500000.0,
            .model_filter = std::nullopt,
            .current_value = 0.0,
        }
    );
    limits.push_back(
        tightrope::db::ApiKeyLimitInput{
            .limit_type = "cost_usd",
            .limit_window = "daily",
            .max_value = 50.0,
            .model_filter = std::string("gpt-5.4"),
            .current_value = 0.0,
        }
    );
    const auto replaced = tightrope::db::replace_api_key_limits(db, created->key_id, limits);
    REQUIRE(replaced.has_value());
    REQUIRE(replaced->size() == 2);

    const auto loaded = tightrope::db::get_api_key_by_key_id(db, created->key_id);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->limits.size() == 2);
    REQUIRE(loaded->limits.front().api_key_id == created->id);

    std::vector<tightrope::db::ApiKeyUsageReservationItemInput> reservation_items;
    reservation_items.push_back(
        tightrope::db::ApiKeyUsageReservationItemInput{
            .metric = "total_tokens",
            .amount = 1000.0,
        }
    );
    const auto reservation =
        tightrope::db::create_api_key_usage_reservation(db, created->key_id, "req-1", reservation_items);
    REQUIRE(reservation.has_value());
    REQUIRE(reservation->request_id == "req-1");
    REQUIRE(reservation->status == "reserved");
    REQUIRE(reservation->items.size() == 1);

    REQUIRE(tightrope::db::transition_api_key_usage_reservation_status(db, "req-1", "reserved", "settled"));
    const auto settled = tightrope::db::get_api_key_usage_reservation(db, "req-1");
    REQUIRE(settled.has_value());
    REQUIRE(settled->status == "settled");

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}
