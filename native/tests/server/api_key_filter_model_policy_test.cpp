#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "controllers/keys_controller.h"
#include "middleware/api_key_filter.h"
#include "migration/migration_runner.h"
#include "repositories/settings_repo.h"

namespace {

std::string make_temp_db_path() {
    static std::uint32_t sequence = 0;
    const auto file = std::filesystem::temp_directory_path() /
                      std::filesystem::path("tightrope-api-key-policy-" + std::to_string(++sequence) + ".sqlite3");
    std::filesystem::remove(file);
    return file.string();
}

} // namespace

TEST_CASE("model policy resolves allowed and enforced models from API key", "[server][middleware][api-key][models]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::db::DashboardSettingsPatch settings_patch;
    settings_patch.api_key_auth_enabled = true;
    REQUIRE(tightrope::db::update_dashboard_settings(db, settings_patch).has_value());

    tightrope::server::controllers::ApiKeyCreateRequest create_request;
    create_request.name = "Policy Key";
    create_request.allowed_models = std::vector<std::string>{"gpt-5.4", "gpt-4.1"};
    create_request.enforced_model = std::string("gpt-5.4");
    create_request.enforced_reasoning_effort = std::string("high");
    const auto created = tightrope::server::controllers::create_api_key(create_request, db);
    REQUIRE(created.status == 201);
    REQUIRE_FALSE(created.key.empty());

    tightrope::proxy::openai::HeaderMap headers{
        {"authorization", "Bearer " + created.key},
    };
    const auto policy = tightrope::server::middleware::resolve_api_key_model_policy(db, headers);
    REQUIRE(policy.enforced_model.has_value());
    REQUIRE(*policy.enforced_model == "gpt-5.4");
    REQUIRE(policy.enforced_reasoning_effort.has_value());
    REQUIRE(*policy.enforced_reasoning_effort == "high");
    REQUIRE(policy.allowed_models.size() == 2);
    REQUIRE(std::find(policy.allowed_models.begin(), policy.allowed_models.end(), "gpt-5.4") != policy.allowed_models.end());
    REQUIRE(std::find(policy.allowed_models.begin(), policy.allowed_models.end(), "gpt-4.1") != policy.allowed_models.end());

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE("request policy enforcement rewrites model and reasoning", "[server][middleware][api-key][models]") {
    tightrope::server::middleware::ApiKeyModelPolicy policy;
    policy.allowed_models = {"gpt-5.4"};
    policy.enforced_model = std::string("gpt-5.4");
    policy.enforced_reasoning_effort = std::string("high");

    std::string body = R"({"model":"gpt-4.1","reasoning":{"effort":"low"},"input":"hello"})";
    const auto decision = tightrope::server::middleware::enforce_api_key_request_policy(policy, body);
    REQUIRE(decision.allow);
    REQUIRE(body.find("\"model\":\"gpt-5.4\"") != std::string::npos);
    REQUIRE(body.find("\"reasoning\":{\"effort\":\"high\"") != std::string::npos);
}

TEST_CASE("request policy enforcement denies disallowed model", "[server][middleware][api-key][models]") {
    tightrope::server::middleware::ApiKeyModelPolicy policy;
    policy.allowed_models = {"gpt-5.4"};

    std::string body = R"({"model":"gpt-4.1","input":"hello"})";
    const auto decision = tightrope::server::middleware::enforce_api_key_request_policy(policy, body);
    REQUIRE_FALSE(decision.allow);
    REQUIRE(decision.status == 403);
    REQUIRE(decision.error_code == "model_not_allowed");
    REQUIRE(decision.error_type == "permission_error");
    REQUIRE(decision.error_body.find("\"code\":\"model_not_allowed\"") != std::string::npos);
}
