#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "migration/migration_runner.h"
#include "controllers/keys_controller.h"

namespace {

std::string make_temp_db_path() {
    const auto file = std::filesystem::temp_directory_path() / std::filesystem::path("tightrope-keys-controller.sqlite3");
    std::filesystem::remove(file);
    return file.string();
}

} // namespace

TEST_CASE("api keys controller supports create list update regenerate delete", "[server][api-keys]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::server::controllers::ApiKeyLimitRuleInput weekly_tokens;
    weekly_tokens.limit_type = "total_tokens";
    weekly_tokens.limit_window = "weekly";
    weekly_tokens.max_value = 100000;

    tightrope::server::controllers::ApiKeyCreateRequest create;
    create.name = "Primary Key";
    create.allowed_models = std::vector<std::string>{"gpt-5.4"};
    create.enforced_model = std::string("gpt-5.4");
    create.enforced_reasoning_effort = std::string("high");
    create.limits.push_back(weekly_tokens);

    const auto created = tightrope::server::controllers::create_api_key(create, db);
    REQUIRE(created.status == 201);
    REQUIRE(created.code.empty());
    REQUIRE(created.api_key.name == "Primary Key");
    REQUIRE(created.api_key.is_active);
    REQUIRE_FALSE(created.api_key.key_id.empty());
    REQUIRE(created.key.rfind("sk-clb-", 0) == 0);
    REQUIRE(created.api_key.key_prefix == created.key.substr(0, created.api_key.key_prefix.size()));
    REQUIRE(created.api_key.limits.size() == 1);
    REQUIRE(created.api_key.limits.front().limit_window == "weekly");

    const auto listed = tightrope::server::controllers::list_api_keys(db);
    REQUIRE(listed.status == 200);
    REQUIRE(listed.items.size() == 1);
    REQUIRE(listed.items.front().key_id == created.api_key.key_id);

    tightrope::server::controllers::ApiKeyUpdateRequest update;
    update.name = "Renamed Key";
    update.is_active = false;
    const auto updated = tightrope::server::controllers::update_api_key(created.api_key.key_id, update, db);
    REQUIRE(updated.status == 200);
    REQUIRE(updated.api_key.name == "Renamed Key");
    REQUIRE_FALSE(updated.api_key.is_active);

    const auto regenerated = tightrope::server::controllers::regenerate_api_key(created.api_key.key_id, db);
    REQUIRE(regenerated.status == 200);
    REQUIRE(regenerated.code.empty());
    REQUIRE(regenerated.key.rfind("sk-clb-", 0) == 0);
    REQUIRE(regenerated.key != created.key);

    const auto deleted = tightrope::server::controllers::delete_api_key(created.api_key.key_id, db);
    REQUIRE(deleted.status == 204);

    const auto final_list = tightrope::server::controllers::list_api_keys(db);
    REQUIRE(final_list.status == 200);
    REQUIRE(final_list.items.empty());

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE("api keys controller rejects invalid payloads", "[server][api-keys]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::server::controllers::ApiKeyCreateRequest missing_name;
    missing_name.name = "   ";
    const auto invalid_name = tightrope::server::controllers::create_api_key(missing_name, db);
    REQUIRE(invalid_name.status == 400);
    REQUIRE(invalid_name.code == "invalid_api_key_payload");

    tightrope::server::controllers::ApiKeyCreateRequest bad_reasoning;
    bad_reasoning.name = "bad";
    bad_reasoning.enforced_reasoning_effort = std::string("turbo");
    const auto invalid_reasoning = tightrope::server::controllers::create_api_key(bad_reasoning, db);
    REQUIRE(invalid_reasoning.status == 400);
    REQUIRE(invalid_reasoning.code == "invalid_api_key_payload");

    tightrope::server::controllers::ApiKeyCreateRequest mismatched_model;
    mismatched_model.name = "bad-model";
    mismatched_model.allowed_models = std::vector<std::string>{"gpt-5.4"};
    mismatched_model.enforced_model = std::string("gpt-5.4-mini");
    const auto invalid_model = tightrope::server::controllers::create_api_key(mismatched_model, db);
    REQUIRE(invalid_model.status == 400);
    REQUIRE(invalid_model.code == "invalid_api_key_payload");

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}
