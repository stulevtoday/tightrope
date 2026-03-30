#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sqlite3.h>

#include "migration/migration_runner.h"
#include "repositories/account_repo.h"
#include "repositories/settings_repo.h"
#include "server.h"
#include "server/runtime_test_utils.h"
#include "controllers/keys_controller.h"
#include "tests/integration/proxy/include/test_support/fake_upstream_transport.h"
#include "usage_fetcher.h"

namespace {

std::string make_temp_db_path() {
    static std::uint32_t sequence = 0;
    const auto file = std::filesystem::temp_directory_path() /
                      std::filesystem::path("tightrope-proxy-runtime-" + std::to_string(++sequence) + ".sqlite3");
    std::filesystem::remove(file);
    return file.string();
}

std::string make_http_post_request(
    const std::string_view path,
    const std::vector<std::pair<std::string, std::string>>& headers,
    const std::string& body
) {
    std::string request = "POST " + std::string(path) + " HTTP/1.1\r\n";
    request += "Host: 127.0.0.1\r\n";
    request += "Connection: close\r\n";
    for (const auto& [key, value] : headers) {
        request += key + ": " + value + "\r\n";
    }
    request += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    request.append(body.data(), body.size());
    return request;
}

struct RequestLogSnapshot {
    std::string path;
    std::string method;
    int status_code = 0;
    std::optional<std::string> model;
    std::optional<std::string> error_code;
    std::optional<std::string> transport;
    std::optional<std::int64_t> account_id;
};

std::optional<RequestLogSnapshot> latest_request_log(sqlite3* db) {
    if (db == nullptr) {
        return std::nullopt;
    }
    constexpr const char* kSql = R"SQL(
SELECT path, method, status_code, model, error_code, transport, account_id
FROM request_logs
ORDER BY id DESC
LIMIT 1;
)SQL";
    sqlite3_stmt* stmt = nullptr;
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

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        finalize();
        return std::nullopt;
    }

    RequestLogSnapshot snapshot{};
    if (const auto* value = sqlite3_column_text(stmt, 0); value != nullptr) {
        snapshot.path = reinterpret_cast<const char*>(value);
    }
    if (const auto* value = sqlite3_column_text(stmt, 1); value != nullptr) {
        snapshot.method = reinterpret_cast<const char*>(value);
    }
    snapshot.status_code = sqlite3_column_int(stmt, 2);
    if (const auto* value = sqlite3_column_text(stmt, 3); value != nullptr) {
        snapshot.model = std::string(reinterpret_cast<const char*>(value));
    }
    if (const auto* value = sqlite3_column_text(stmt, 4); value != nullptr) {
        snapshot.error_code = std::string(reinterpret_cast<const char*>(value));
    }
    if (const auto* value = sqlite3_column_text(stmt, 5); value != nullptr) {
        snapshot.transport = std::string(reinterpret_cast<const char*>(value));
    }
    if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) {
        snapshot.account_id = sqlite3_column_int64(stmt, 6);
    }

    finalize();
    return snapshot;
}

std::int64_t count_usage_reservations_with_status(sqlite3* db, const std::string_view status) {
    if (db == nullptr) {
        return 0;
    }
    constexpr const char* kSql = "SELECT COUNT(1) FROM api_key_usage_reservations WHERE status = ?1;";
    sqlite3_stmt* stmt = nullptr;
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
    if (sqlite3_bind_text(stmt, 1, std::string(status).c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        finalize();
        return 0;
    }
    std::int64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int64(stmt, 0);
    }
    finalize();
    return count;
}

class StaticUsageValidator final : public tightrope::usage::UsageValidator {
public:
    explicit StaticUsageValidator(tightrope::usage::UsageValidationResult result) : result_(std::move(result)) {}

    [[nodiscard]] tightrope::usage::UsageValidationResult validate(
        std::string_view /*access_token*/,
        std::string_view /*account_id*/
    ) override {
        return result_;
    }

private:
    tightrope::usage::UsageValidationResult result_;
};

class ScopedUsageValidator final {
public:
    explicit ScopedUsageValidator(std::shared_ptr<tightrope::usage::UsageValidator> validator) {
        tightrope::usage::set_usage_validator_for_testing(std::move(validator));
    }

    ~ScopedUsageValidator() {
        tightrope::usage::clear_usage_validator_for_testing();
    }
};

class StaticUsagePayloadFetcher final : public tightrope::usage::UsagePayloadFetcher {
public:
    explicit StaticUsagePayloadFetcher(std::optional<tightrope::usage::UsagePayloadSnapshot> payload)
        : payload_(std::move(payload)) {}

    [[nodiscard]] std::optional<tightrope::usage::UsagePayloadSnapshot> fetch(
        std::string_view /*access_token*/,
        std::string_view /*account_id*/
    ) override {
        return payload_;
    }

private:
    std::optional<tightrope::usage::UsagePayloadSnapshot> payload_;
};

class ScopedUsagePayloadFetcher final {
public:
    explicit ScopedUsagePayloadFetcher(std::shared_ptr<tightrope::usage::UsagePayloadFetcher> fetcher) {
        tightrope::usage::set_usage_payload_fetcher_for_testing(std::move(fetcher));
    }

    ~ScopedUsagePayloadFetcher() {
        tightrope::usage::clear_usage_payload_fetcher_for_testing();
    }
};

class ScopedUsagePayloadCache final {
public:
    ScopedUsagePayloadCache(std::string account_id, std::optional<tightrope::usage::UsagePayloadSnapshot> payload)
        : account_id_(std::move(account_id)) {
        tightrope::usage::seed_usage_payload_cache_for_testing(account_id_, std::move(payload));
    }

    ~ScopedUsagePayloadCache() {
        tightrope::usage::clear_usage_payload_cache_for_testing();
    }

private:
    std::string account_id_;
};

} // namespace

TEST_CASE("runtime enforces API key auth on protected proxy routes", "[server][runtime][proxy][auth]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::db::DashboardSettingsPatch settings_patch;
    settings_patch.api_key_auth_enabled = true;
    REQUIRE(tightrope::db::update_dashboard_settings(db, settings_patch).has_value());

    tightrope::server::controllers::ApiKeyCreateRequest create_request;
    create_request.name = "Runtime Key";
    const auto created_key = tightrope::server::controllers::create_api_key(create_request, db);
    REQUIRE(created_key.status == 201);
    REQUIRE_FALSE(created_key.key.empty());

    sqlite3_close(db);

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_auth","object":"response","status":"completed","output":[]})",
            };
        }
    );
    tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const std::string payload = R"({"model":"gpt-5.4","input":"auth-check"})";
    const auto denied_response = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request("/v1/responses", {{"Content-Type", "application/json"}}, payload)
    );
    REQUIRE(denied_response.find("401 Unauthorized") != std::string::npos);
    REQUIRE(denied_response.find("\"code\":\"invalid_api_key\"") != std::string::npos);
    REQUIRE(fake->call_count == 0);

    const auto allowed_response = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request(
            "/v1/responses",
            {
                {"Content-Type", "application/json"},
                {"Authorization", "Bearer " + created_key.key},
            },
            payload
        )
    );
    REQUIRE(allowed_response.find("200 OK") != std::string::npos);
    REQUIRE(allowed_response.find("\"object\":\"response\"") != std::string::npos);
    REQUIRE(fake->call_count == 1);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("runtime validates content encoding and propagates request id", "[server][runtime][proxy][middleware]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));
    sqlite3_close(db);

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_decompressed","object":"response","status":"completed","output":[]})",
            };
        }
    );
    tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const std::string plain_payload = R"({"model":"gpt-5.4","input":"middleware"})";

    const auto invalid_encoding = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request(
            "/v1/responses",
            {
                {"Content-Type", "application/json"},
                {"Content-Encoding", "br"},
                {"X-Request-Id", "req-encoding-denied"},
            },
            plain_payload
        )
    );
    REQUIRE(invalid_encoding.find("400 Bad Request") != std::string::npos);
    REQUIRE(invalid_encoding.find("\"Unsupported Content-Encoding\"") != std::string::npos);
    REQUIRE(
        tightrope::tests::server::http_header_value(invalid_encoding, "x-request-id").value_or("") ==
        "req-encoding-denied");
    REQUIRE(fake->call_count == 0);

    const auto ok = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request(
            "/v1/responses",
            {
                {"Content-Type", "application/json"},
                {"X-Request-Id", "req-propagate-001"},
            },
            plain_payload
        )
    );
    REQUIRE(ok.find("200 OK") != std::string::npos);
    REQUIRE(tightrope::tests::server::http_header_value(ok, "x-request-id").value_or("") == "req-propagate-001");

    REQUIRE(fake->call_count == 1);
    REQUIRE(fake->last_plan.body.find("\"model\":\"gpt-5.4\"") != std::string::npos);
    REQUIRE(fake->last_plan.headers.find("x-request-id") != fake->last_plan.headers.end());
    REQUIRE(fake->last_plan.headers.at("x-request-id") == "req-propagate-001");

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("runtime responses routes advertise and honor downstream turn-state headers", "[server][runtime][proxy][turn-state]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));
    sqlite3_close(db);

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            bool wants_sse = false;
            const auto accept_it = plan.headers.find("Accept");
            if (accept_it != plan.headers.end()) {
                wants_sse = accept_it->second.find("text/event-stream") != std::string::npos;
            }
            const auto accept_lower_it = plan.headers.find("accept");
            if (!wants_sse && accept_lower_it != plan.headers.end()) {
                wants_sse = accept_lower_it->second.find("text/event-stream") != std::string::npos;
            }

            if (wants_sse) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 200,
                    .events = {
                        R"({"type":"response.created","response":{"id":"resp_turn_state","status":"in_progress"}})",
                        R"({"type":"response.completed","response":{"id":"resp_turn_state","status":"completed","output":[]}})",
                    },
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_turn_state","object":"response","status":"completed","output":[]})",
            };
        }
    );
    tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const std::string payload = R"({"model":"gpt-5.4","input":"turn-state"})";
    const auto json_default = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request("/v1/responses", {{"Content-Type", "application/json"}}, payload)
    );
    REQUIRE(json_default.find("200 OK") != std::string::npos);
    const auto json_default_turn_state =
        tightrope::tests::server::http_header_value(json_default, "x-codex-turn-state");
    REQUIRE(json_default_turn_state.has_value());
    REQUIRE(json_default_turn_state->rfind("http_turn_", 0) == 0);

    const auto json_existing = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request(
            "/v1/responses",
            {
                {"Content-Type", "application/json"},
                {"x-codex-turn-state", "existing-json-turn"},
            },
            payload
        )
    );
    REQUIRE(json_existing.find("200 OK") != std::string::npos);
    REQUIRE(
        tightrope::tests::server::http_header_value(json_existing, "x-codex-turn-state") ==
        std::optional<std::string>{"existing-json-turn"}
    );

    const auto sse_default = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request(
            "/v1/responses",
            {
                {"Content-Type", "application/json"},
                {"Accept", "text/event-stream"},
            },
            payload
        )
    );
    REQUIRE(sse_default.find("HTTP/1.1") != std::string::npos);
    const auto sse_turn_state = tightrope::tests::server::http_header_value(sse_default, "x-codex-turn-state");
    REQUIRE(sse_turn_state.has_value());
    REQUIRE(sse_turn_state->rfind("http_turn_", 0) == 0);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("runtime codex usage route requires identity headers and returns plan type", "[server][runtime][usage]") {
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
    account.access_token_encrypted = "enc-access";
    account.refresh_token_encrypted = "enc-refresh";
    account.id_token_encrypted = "enc-id";
    REQUIRE(tightrope::db::upsert_oauth_account(db, account).has_value());

    sqlite3_close(db);

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(db_path));
    ScopedUsageValidator scoped_usage_validator(
        std::make_shared<StaticUsageValidator>(tightrope::usage::UsageValidationResult{
            .success = true,
            .status_code = 200,
            .message = "",
        })
    );
    tightrope::usage::UsagePayloadSnapshot payload_snapshot;
    payload_snapshot.plan_type = "plus";
    payload_snapshot.rate_limit = tightrope::usage::UsageRateLimitDetails{
        .allowed = true,
        .limit_reached = false,
        .primary_window = tightrope::usage::UsageWindowSnapshot{
            .used_percent = 10,
            .limit_window_seconds = 10800,
            .reset_after_seconds = 7200,
            .reset_at = 1'700'000'123,
        },
        .secondary_window = tightrope::usage::UsageWindowSnapshot{
            .used_percent = 30,
            .limit_window_seconds = 604800,
            .reset_after_seconds = 86400,
            .reset_at = 1'700'800'123,
        },
    };
    payload_snapshot.credits = tightrope::usage::UsageCreditsDetails{
        .has_credits = true,
        .unlimited = false,
        .balance = "12.34",
    };
    payload_snapshot.additional_rate_limits.push_back(
        tightrope::usage::UsageAdditionalRateLimit{
            .quota_key = "rqm",
            .limit_name = "requests_per_minute",
            .display_label = "Requests / minute",
            .metered_feature = "responses",
            .rate_limit = tightrope::usage::UsageRateLimitDetails{
                .allowed = true,
                .limit_reached = false,
                .primary_window = tightrope::usage::UsageWindowSnapshot{
                    .used_percent = 42,
                    .limit_window_seconds = 60,
                    .reset_after_seconds = 15,
                    .reset_at = 1'700'000'456,
                },
                .secondary_window = std::nullopt,
            },
        }
    );
    ScopedUsagePayloadFetcher scoped_payload_fetcher(
        std::make_shared<StaticUsagePayloadFetcher>(std::move(payload_snapshot))
    );

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const auto no_auth = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/codex/usage HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(no_auth.find("401 Unauthorized") != std::string::npos);

    const auto unknown_account = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/codex/usage HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Authorization: Bearer any-token\r\n"
        "chatgpt-account-id: acc-missing\r\n"
        "Connection: close\r\n\r\n"
    );
    REQUIRE(unknown_account.find("401 Unauthorized") != std::string::npos);

    const auto ok = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/codex/usage HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Authorization: Bearer any-token\r\n"
        "chatgpt-account-id: acc-usage-001\r\n"
        "Connection: close\r\n\r\n"
    );
    REQUIRE(ok.find("200 OK") != std::string::npos);
    REQUIRE(ok.find("\"plan_type\":\"plus\"") != std::string::npos);
    REQUIRE(ok.find("\"rate_limit\":{\"allowed\":true,\"limit_reached\":false") != std::string::npos);
    REQUIRE(ok.find("\"used_percent\":10") != std::string::npos);
    REQUIRE(ok.find("\"used_percent\":30") != std::string::npos);
    REQUIRE(ok.find("\"has_credits\":true") != std::string::npos);
    REQUIRE(ok.find("\"unlimited\":false") != std::string::npos);
    REQUIRE(ok.find("\"balance\":\"12.34\"") != std::string::npos);
    REQUIRE(ok.find("\"additional_rate_limits\":[") != std::string::npos);
    REQUIRE(ok.find("\"limit_name\":\"requests_per_minute\"") != std::string::npos);
    REQUIRE(ok.find("\"metered_feature\":\"responses\"") != std::string::npos);
    REQUIRE(ok.find("\"quota_key\":\"rqm\"") != std::string::npos);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("runtime codex usage route maps upstream identity validation failures", "[server][runtime][usage]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::db::OauthAccountUpsert account;
    account.email = "usage-auth@example.com";
    account.provider = "openai";
    account.chatgpt_account_id = "acc-usage-auth-001";
    account.plan_type = "plus";
    account.access_token_encrypted = "enc-access";
    account.refresh_token_encrypted = "enc-refresh";
    account.id_token_encrypted = "enc-id";
    REQUIRE(tightrope::db::upsert_oauth_account(db, account).has_value());
    sqlite3_close(db);

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(db_path));

    SECTION("upstream auth rejection returns 401") {
        ScopedUsageValidator scoped_usage_validator(
            std::make_shared<StaticUsageValidator>(tightrope::usage::UsageValidationResult{
                .success = false,
                .status_code = 401,
                .message = "invalid token",
            })
        );
        tightrope::server::Runtime runtime;
        const auto port = tightrope::tests::server::next_runtime_port();
        REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

        const auto response = tightrope::tests::server::send_raw_http(
            port,
            "GET /api/codex/usage HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Authorization: Bearer any-token\r\n"
            "chatgpt-account-id: acc-usage-auth-001\r\n"
            "Connection: close\r\n\r\n"
        );
        REQUIRE(response.find("401 Unauthorized") != std::string::npos);
        REQUIRE(response.find("\"Invalid ChatGPT token or chatgpt-account-id\"") != std::string::npos);
        REQUIRE(runtime.stop());
    }

    SECTION("upstream rate limit returns 429") {
        ScopedUsageValidator scoped_usage_validator(
            std::make_shared<StaticUsageValidator>(tightrope::usage::UsageValidationResult{
                .success = false,
                .status_code = 429,
                .message = "usage quota exceeded",
            })
        );
        tightrope::server::Runtime runtime;
        const auto port = tightrope::tests::server::next_runtime_port();
        REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

        const auto response = tightrope::tests::server::send_raw_http(
            port,
            "GET /api/codex/usage HTTP/1.1\r\n"
            "Host: 127.0.0.1\r\n"
            "Authorization: Bearer any-token\r\n"
            "chatgpt-account-id: acc-usage-auth-001\r\n"
            "Connection: close\r\n\r\n"
        );
        REQUIRE(response.find("429") != std::string::npos);
        REQUIRE(response.find("\"code\":\"rate_limit_exceeded\"") != std::string::npos);
        REQUIRE(runtime.stop());
    }

    std::filesystem::remove(db_path);
}

TEST_CASE("runtime compact responses include codex-lb style rate-limit headers", "[server][runtime][proxy][headers]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::db::OauthAccountUpsert account;
    account.email = "headers@example.com";
    account.provider = "openai";
    account.chatgpt_account_id = "acc-header-001";
    account.plan_type = "plus";
    account.access_token_encrypted = "enc-access";
    account.refresh_token_encrypted = "enc-refresh";
    account.id_token_encrypted = "enc-id";
    REQUIRE(tightrope::db::upsert_oauth_account(db, account).has_value());
    sqlite3_close(db);

    tightrope::usage::UsagePayloadSnapshot payload_snapshot;
    payload_snapshot.plan_type = "plus";
    payload_snapshot.rate_limit = tightrope::usage::UsageRateLimitDetails{
        .allowed = true,
        .limit_reached = false,
        .primary_window = tightrope::usage::UsageWindowSnapshot{
            .used_percent = 25,
            .limit_window_seconds = 18000,
            .reset_after_seconds = 0,
            .reset_at = 1'735'689'600,
        },
        .secondary_window = tightrope::usage::UsageWindowSnapshot{
            .used_percent = 80,
            .limit_window_seconds = 604800,
            .reset_after_seconds = 0,
            .reset_at = 1'735'862'400,
        },
    };
    payload_snapshot.credits = tightrope::usage::UsageCreditsDetails{
        .has_credits = true,
        .unlimited = false,
        .balance = "12.5",
    };
    ScopedUsagePayloadCache scoped_payload_cache("acc-header-001", std::move(payload_snapshot));

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_compact_headers","status":"completed","output":[{"type":"output_text","text":"ok"}]})",
            };
        }
    );
    tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const auto response = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request(
            "/v1/responses/compact",
            {{"Content-Type", "application/json"}},
            R"({"model":"gpt-5.4","input":"header-check"})"
        )
    );
    REQUIRE(response.find("200 OK") != std::string::npos);
    REQUIRE(
        tightrope::tests::server::http_header_value(response, "x-codex-primary-used-percent") ==
        std::optional<std::string>{"25.0"}
    );
    REQUIRE(
        tightrope::tests::server::http_header_value(response, "x-codex-primary-window-minutes") ==
        std::optional<std::string>{"300"}
    );
    REQUIRE(
        tightrope::tests::server::http_header_value(response, "x-codex-primary-reset-at") ==
        std::optional<std::string>{"1735689600"}
    );
    REQUIRE(
        tightrope::tests::server::http_header_value(response, "x-codex-secondary-used-percent") ==
        std::optional<std::string>{"80.0"}
    );
    REQUIRE(
        tightrope::tests::server::http_header_value(response, "x-codex-secondary-window-minutes") ==
        std::optional<std::string>{"10080"}
    );
    REQUIRE(
        tightrope::tests::server::http_header_value(response, "x-codex-secondary-reset-at") ==
        std::optional<std::string>{"1735862400"}
    );
    REQUIRE(
        tightrope::tests::server::http_header_value(response, "x-codex-credits-has-credits") ==
        std::optional<std::string>{"true"}
    );
    REQUIRE(
        tightrope::tests::server::http_header_value(response, "x-codex-credits-unlimited") ==
        std::optional<std::string>{"false"}
    );
    REQUIRE(
        tightrope::tests::server::http_header_value(response, "x-codex-credits-balance") ==
        std::optional<std::string>{"12.50"}
    );

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("runtime chat completions route supports json and streaming responses", "[server][runtime][proxy][chat]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));
    sqlite3_close(db);

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    auto call_index = std::make_shared<int>(0);
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [call_index](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            *call_index += 1;
            if (*call_index == 2) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 200,
                    .events = {
                        R"({"contract":"proxy-streaming-v1","type":"response.output_text.delta","delta":"hi"})",
                        R"({"contract":"proxy-streaming-v1","type":"response.completed","response":{"id":"resp_stream","usage":{"input_tokens":1,"output_tokens":1,"total_tokens":2}}})",
                    },
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body =
                    R"({"id":"resp_json","object":"response","model":"gpt-5.4","status":"completed","output":[{"type":"message","content":[{"type":"output_text","text":"hello from upstream"}]}],"usage":{"input_tokens":3,"output_tokens":4,"total_tokens":7}})",
            };
        }
    );
    tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto json_response = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request(
            "/v1/chat/completions",
            {{"Content-Type", "application/json"}},
            R"({"model":"gpt-5.4","messages":[{"role":"user","content":"hello"}]})"
        )
    );
    REQUIRE(json_response.find("200 OK") != std::string::npos);
    REQUIRE(json_response.find("\"object\":\"chat.completion\"") != std::string::npos);
    REQUIRE(json_response.find("\"hello from upstream\"") != std::string::npos);

    const auto sse_response = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request(
            "/v1/chat/completions",
            {{"Content-Type", "application/json"}},
            R"({"model":"gpt-5.4","stream":true,"stream_options":{"include_usage":true},"messages":[{"role":"user","content":"hello"}]})"
        )
    );
    REQUIRE(sse_response.find("200 OK") != std::string::npos);
    REQUIRE(sse_response.find("text/event-stream") != std::string::npos);
    REQUIRE(sse_response.find("\"object\":\"chat.completion.chunk\"") != std::string::npos);
    REQUIRE(sse_response.find("\"content\":\"hi\"") != std::string::npos);
    REQUIRE(sse_response.find("\"usage\":null") != std::string::npos);
    REQUIRE(sse_response.find("\"total_tokens\":2") != std::string::npos);
    REQUIRE(sse_response.find("\"usage\"") != std::string::npos);
    REQUIRE(sse_response.find("[DONE]") != std::string::npos);

    REQUIRE(fake->call_count == 2);
    REQUIRE(fake->last_plan.path == "/codex/responses");

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("runtime chat completions maps payload fields to responses contract", "[server][runtime][proxy][chat]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));
    sqlite3_close(db);

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(db_path));

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_mapped","object":"response","model":"gpt-5.4","status":"completed","output":[]})",
            };
        }
    );
    tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const auto response = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request(
            "/v1/chat/completions",
            {{"Content-Type", "application/json"}},
            R"({
                "model":"gpt-5.4",
                "messages":[
                    {"role":"system","content":"sys prompt"},
                    {"role":"user","content":[
                        {"type":"text","text":"hello"},
                        {"type":"image_url","image_url":{"url":"https://example.com/a.png","detail":"high"}},
                        {"type":"file","file":{"file_url":"https://example.com/file.pdf"}}
                    ]}
                ],
                "response_format":{
                    "type":"json_schema",
                    "json_schema":{
                        "name":"result_schema",
                        "schema":{"type":"object","properties":{"ok":{"type":"boolean"}}},
                        "strict":true
                    }
                },
                "tools":[
                    {"type":"function","function":{"name":"do_thing","description":"desc","parameters":{"type":"object","properties":{}}}},
                    {"type":"web_search_preview"}
                ],
                "tool_choice":{"type":"function","function":{"name":"do_thing"}},
                "stream_options":{"include_obfuscation":true}
            })"
        )
    );
    REQUIRE(response.find("200 OK") != std::string::npos);
    REQUIRE(fake->call_count == 1);

    const auto& transformed_payload = fake->last_plan.body;
    REQUIRE(transformed_payload.find("\"model\":\"gpt-5.4\"") != std::string::npos);
    REQUIRE(transformed_payload.find("\"instructions\":\"sys prompt\"") != std::string::npos);
    REQUIRE(transformed_payload.find("\"messages\"") == std::string::npos);
    REQUIRE(transformed_payload.find("\"response_format\"") == std::string::npos);
    REQUIRE(transformed_payload.find("\"stream\":true") != std::string::npos);
    REQUIRE(transformed_payload.find("\"stream_options\":{\"include_obfuscation\":true}") != std::string::npos);
    REQUIRE(transformed_payload.find("\"text\":{\"format\":{\"type\":\"json_schema\"") != std::string::npos);
    REQUIRE(transformed_payload.find("\"name\":\"result_schema\"") != std::string::npos);
    REQUIRE(transformed_payload.find("\"schema\":{\"type\":\"object\"") != std::string::npos);
    REQUIRE(transformed_payload.find("\"tools\":[") != std::string::npos);
    REQUIRE(transformed_payload.find("\"type\":\"function\"") != std::string::npos);
    REQUIRE(transformed_payload.find("\"name\":\"do_thing\"") != std::string::npos);
    REQUIRE(transformed_payload.find("\"type\":\"web_search\"") != std::string::npos);
    REQUIRE(transformed_payload.find("\"tool_choice\":{\"type\":\"function\",\"name\":\"do_thing\"}") != std::string::npos);
    REQUIRE(transformed_payload.find("\"input\":[{\"role\":\"user\"") != std::string::npos);
    REQUIRE(transformed_payload.find("\"type\":\"input_text\",\"text\":\"hello\"") != std::string::npos);
    REQUIRE(transformed_payload.find("\"type\":\"input_image\",\"image_url\":\"https://example.com/a.png\",\"detail\":\"high\"")
            != std::string::npos);
    REQUIRE(transformed_payload.find("\"type\":\"input_file\",\"file_url\":\"https://example.com/file.pdf\"") != std::string::npos);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("runtime chat completions maps tool calls for JSON and streaming", "[server][runtime][proxy][chat]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));
    sqlite3_close(db);

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(db_path));

    auto call_index = std::make_shared<int>(0);
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [call_index](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            *call_index += 1;
            if (*call_index == 2) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 200,
                    .events =
                        {
                            R"({"contract":"proxy-streaming-v1","type":"response.function_call_arguments.delta","call_id":"call_stream_1","name":"lookup","delta":"{\"city\":\"Paris\"}"})",
                            R"({"contract":"proxy-streaming-v1","type":"response.completed","response":{"id":"resp_tool_stream","usage":{"input_tokens":1,"output_tokens":2,"total_tokens":3}}})",
                        },
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body =
                    R"({"id":"resp_tool_json","object":"response","model":"gpt-5.4","status":"completed","output":[{"type":"function_call","call_id":"call_json_1","name":"lookup","arguments":"{\"city\":\"Paris\"}"}]})",
            };
        }
    );
    tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const auto json_response = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request(
            "/v1/chat/completions",
            {{"Content-Type", "application/json"}},
            R"({"model":"gpt-5.4","messages":[{"role":"user","content":"run tool"}]})"
        )
    );
    REQUIRE(json_response.find("200 OK") != std::string::npos);
    REQUIRE(json_response.find("\"tool_calls\"") != std::string::npos);
    REQUIRE(json_response.find("\"call_json_1\"") != std::string::npos);
    REQUIRE(json_response.find("\"finish_reason\":\"tool_calls\"") != std::string::npos);

    const auto sse_response = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request(
            "/v1/chat/completions",
            {{"Content-Type", "application/json"}},
            R"({"model":"gpt-5.4","stream":true,"messages":[{"role":"user","content":"run tool stream"}]})"
        )
    );
    REQUIRE(sse_response.find("200 OK") != std::string::npos);
    REQUIRE(sse_response.find("\"tool_calls\"") != std::string::npos);
    REQUIRE(sse_response.find("\"call_stream_1\"") != std::string::npos);
    REQUIRE(sse_response.find("\"finish_reason\":\"tool_calls\"") != std::string::npos);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("runtime chat completions rejects invalid compatibility payloads", "[server][runtime][proxy][chat]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));
    sqlite3_close(db);

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(db_path));

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_unexpected","object":"response","status":"completed","output":[]})",
            };
        }
    );
    tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    SECTION("developer messages must be text-only") {
        const auto response = tightrope::tests::server::send_raw_http(
            port,
            make_http_post_request(
                "/v1/chat/completions",
                {{"Content-Type", "application/json"}},
                R"({"model":"gpt-5.4","messages":[{"role":"developer","content":[{"type":"image_url","image_url":{"url":"https://example.com/a.png"}}]},{"role":"user","content":"hi"}]})"
            )
        );
        REQUIRE(response.find("400 Bad Request") != std::string::npos);
        REQUIRE(response.find("\"param\":\"messages\"") != std::string::npos);
    }

    SECTION("file_id is rejected with messages param") {
        const auto response = tightrope::tests::server::send_raw_http(
            port,
            make_http_post_request(
                "/v1/chat/completions",
                {{"Content-Type", "application/json"}},
                R"({"model":"gpt-5.4","messages":[{"role":"user","content":[{"type":"text","text":"summarize"},{"type":"file","file":{"file_id":"file-123"}}]}]})"
            )
        );
        REQUIRE(response.find("400 Bad Request") != std::string::npos);
        REQUIRE(response.find("\"message\":\"Invalid request payload\"") != std::string::npos);
        REQUIRE(response.find("\"param\":\"messages\"") != std::string::npos);
    }

    SECTION("input_audio is rejected") {
        const auto response = tightrope::tests::server::send_raw_http(
            port,
            make_http_post_request(
                "/v1/chat/completions",
                {{"Content-Type", "application/json"}},
                R"({"model":"gpt-5.4","messages":[{"role":"user","content":[{"type":"text","text":"transcribe"},{"type":"input_audio","input_audio":{"data":"AAA","format":"wav"}}]}]})"
            )
        );
        REQUIRE(response.find("400 Bad Request") != std::string::npos);
        REQUIRE(response.find("\"message\":\"Audio input is not supported.\"") != std::string::npos);
        REQUIRE(response.find("\"param\":\"messages\"") != std::string::npos);
    }

    SECTION("missing response_format json_schema is rejected") {
        const auto response = tightrope::tests::server::send_raw_http(
            port,
            make_http_post_request(
                "/v1/chat/completions",
                {{"Content-Type", "application/json"}},
                R"({"model":"gpt-5.4","messages":[{"role":"user","content":"hi"}],"response_format":{"type":"json_schema"}})"
            )
        );
        REQUIRE(response.find("400 Bad Request") != std::string::npos);
        REQUIRE(response.find("\"param\":\"response_format\"") != std::string::npos);
    }

    SECTION("unsupported built-in tools are rejected before upstream") {
        const auto response = tightrope::tests::server::send_raw_http(
            port,
            make_http_post_request(
                "/v1/chat/completions",
                {{"Content-Type", "application/json"}},
                R"({"model":"gpt-5.4","messages":[{"role":"user","content":"hi"}],"tools":[{"type":"image_generation"}]})"
            )
        );
        REQUIRE(response.find("400 Bad Request") != std::string::npos);
        REQUIRE(fake->call_count == 0);
    }

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("runtime enforces API-key model and reasoning policy across proxy routes", "[server][runtime][proxy][policy]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::db::DashboardSettingsPatch settings_patch;
    settings_patch.api_key_auth_enabled = true;
    REQUIRE(tightrope::db::update_dashboard_settings(db, settings_patch).has_value());

    tightrope::server::controllers::ApiKeyCreateRequest deny_request;
    deny_request.name = "Policy Deny Key";
    deny_request.allowed_models = std::vector<std::string>{"gpt-5.4"};
    const auto deny_key = tightrope::server::controllers::create_api_key(deny_request, db);
    REQUIRE(deny_key.status == 201);
    REQUIRE_FALSE(deny_key.key.empty());

    tightrope::server::controllers::ApiKeyCreateRequest enforce_request;
    enforce_request.name = "Policy Enforce Key";
    enforce_request.allowed_models = std::vector<std::string>{"gpt-5.4"};
    enforce_request.enforced_model = std::string("gpt-5.4");
    enforce_request.enforced_reasoning_effort = std::string("high");
    const auto enforce_key = tightrope::server::controllers::create_api_key(enforce_request, db);
    REQUIRE(enforce_key.status == 201);
    REQUIRE_FALSE(enforce_key.key.empty());

    sqlite3_close(db);

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            if (plan.path == "/codex/responses/compact") {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 200,
                    .body = R"({"status":"ok","output_text":"compact"})",
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body =
                    R"({"id":"resp_policy","object":"response","model":"gpt-5.4","status":"completed","output":[{"type":"message","content":[{"type":"output_text","text":"ok"}]}]})",
            };
        }
    );
    tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const auto denied = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request(
            "/v1/responses",
            {
                {"Content-Type", "application/json"},
                {"Authorization", "Bearer " + deny_key.key},
            },
            R"({"model":"gpt-4.1","input":"denied"})"
        )
    );
    REQUIRE(denied.find("403 Forbidden") != std::string::npos);
    REQUIRE(denied.find("\"code\":\"model_not_allowed\"") != std::string::npos);
    REQUIRE(fake->call_count == 0);

    const auto enforced_responses = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request(
            "/v1/responses",
            {
                {"Content-Type", "application/json"},
                {"Authorization", "Bearer " + enforce_key.key},
            },
            R"({"model":"gpt-4.1","reasoning":{"effort":"low"},"input":"policy"})"
        )
    );
    REQUIRE(enforced_responses.find("200 OK") != std::string::npos);
    REQUIRE(fake->call_count == 1);
    REQUIRE(fake->last_plan.path == "/codex/responses");
    REQUIRE(fake->last_plan.body.find("\"model\":\"gpt-5.4\"") != std::string::npos);
    REQUIRE(fake->last_plan.body.find("\"reasoning\":{\"effort\":\"high\"") != std::string::npos);

    const auto enforced_compact = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request(
            "/v1/responses/compact",
            {
                {"Content-Type", "application/json"},
                {"Authorization", "Bearer " + enforce_key.key},
            },
            R"({"model":"gpt-4.1","reasoning":{"effort":"low"},"input":"compact"})"
        )
    );
    REQUIRE(enforced_compact.find("200 OK") != std::string::npos);
    REQUIRE(fake->call_count == 2);
    REQUIRE(fake->last_plan.path == "/codex/responses/compact");
    REQUIRE(fake->last_plan.body.find("\"model\":\"gpt-5.4\"") != std::string::npos);
    REQUIRE(fake->last_plan.body.find("\"reasoning\":{\"effort\":\"high\"") != std::string::npos);

    const auto enforced_chat = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request(
            "/v1/chat/completions",
            {
                {"Content-Type", "application/json"},
                {"Authorization", "Bearer " + enforce_key.key},
            },
            R"({"model":"gpt-4.1","reasoning_effort":"low","messages":[{"role":"user","content":"hello"}]})"
        )
    );
    REQUIRE(enforced_chat.find("200 OK") != std::string::npos);
    REQUIRE(fake->call_count == 3);
    REQUIRE(fake->last_plan.path == "/codex/responses");
    REQUIRE(fake->last_plan.body.find("\"model\":\"gpt-5.4\"") != std::string::npos);
    REQUIRE(fake->last_plan.body.find("\"reasoning\":{\"effort\":\"high\"") != std::string::npos);

    REQUIRE(runtime.stop());

    sqlite3* verify_db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &verify_db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    REQUIRE(verify_db != nullptr);
    REQUIRE(count_usage_reservations_with_status(verify_db, "settled") == 3);
    REQUIRE(count_usage_reservations_with_status(verify_db, "released") == 0);
    sqlite3_close(verify_db);

    std::filesystem::remove(db_path);
}

TEST_CASE("runtime enforces API-key model policy on transcribe route", "[server][runtime][proxy][policy][transcribe]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::db::DashboardSettingsPatch settings_patch;
    settings_patch.api_key_auth_enabled = true;
    REQUIRE(tightrope::db::update_dashboard_settings(db, settings_patch).has_value());

    tightrope::server::controllers::ApiKeyCreateRequest deny_request;
    deny_request.name = "Transcribe Deny Key";
    deny_request.allowed_models = std::vector<std::string>{"gpt-5.4"};
    const auto deny_key = tightrope::server::controllers::create_api_key(deny_request, db);
    REQUIRE(deny_key.status == 201);
    REQUIRE_FALSE(deny_key.key.empty());

    tightrope::server::controllers::ApiKeyCreateRequest allow_request;
    allow_request.name = "Transcribe Allow Key";
    allow_request.allowed_models = std::vector<std::string>{"gpt-4o-transcribe"};
    const auto allow_key = tightrope::server::controllers::create_api_key(allow_request, db);
    REQUIRE(allow_key.status == 201);
    REQUIRE_FALSE(allow_key.key.empty());
    sqlite3_close(db);

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"text":"ok"})",
            };
        }
    );
    tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const std::string transcribe_payload =
        R"({"model":"gpt-4o-transcribe","prompt":"hello","audio_bytes":"AAAA"})";

    const auto denied = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request(
            "/backend-api/transcribe",
            {
                {"Content-Type", "application/json"},
                {"Authorization", "Bearer " + deny_key.key},
            },
            transcribe_payload
        )
    );
    REQUIRE(denied.find("403 Forbidden") != std::string::npos);
    REQUIRE(denied.find("\"code\":\"model_not_allowed\"") != std::string::npos);
    REQUIRE(fake->call_count == 0);

    const auto allowed = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request(
            "/backend-api/transcribe",
            {
                {"Content-Type", "application/json"},
                {"Authorization", "Bearer " + allow_key.key},
            },
            transcribe_payload
        )
    );
    REQUIRE(allowed.find("200 OK") != std::string::npos);
    REQUIRE(fake->call_count == 1);
    REQUIRE(fake->last_plan.path == "/transcribe");

    REQUIRE(runtime.stop());

    sqlite3* verify_db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &verify_db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    REQUIRE(verify_db != nullptr);
    REQUIRE(count_usage_reservations_with_status(verify_db, "settled") == 1);
    REQUIRE(count_usage_reservations_with_status(verify_db, "released") == 0);
    sqlite3_close(verify_db);

    std::filesystem::remove(db_path);
}

TEST_CASE("runtime persists successful responses request logs with http transport", "[server][runtime][proxy][request-log]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::db::OauthAccountUpsert account;
    account.email = "logs-success@example.com";
    account.provider = "openai";
    account.chatgpt_account_id = "acc-log-001";
    account.plan_type = "plus";
    account.access_token_encrypted = "enc-access";
    account.refresh_token_encrypted = "enc-refresh";
    account.id_token_encrypted = "enc-id";
    REQUIRE(tightrope::db::upsert_oauth_account(db, account).has_value());
    sqlite3_close(db);

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_log_ok","object":"response","status":"completed","output":[]})",
            };
        }
    );
    tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const auto response = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request(
            "/v1/responses",
            {
                {"Content-Type", "application/json"},
                {"chatgpt-account-id", "acc-log-001"},
            },
            R"({"model":"gpt-5.4","input":"persist-success"})"
        )
    );
    REQUIRE(response.find("200 OK") != std::string::npos);

    REQUIRE(runtime.stop());

    sqlite3* read_db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &read_db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    REQUIRE(read_db != nullptr);
    const auto logged = latest_request_log(read_db);
    REQUIRE(logged.has_value());
    REQUIRE(logged->path == "/v1/responses");
    REQUIRE(logged->method == "POST");
    REQUIRE(logged->status_code == 200);
    REQUIRE(logged->model.has_value());
    REQUIRE(*logged->model == "gpt-5.4");
    REQUIRE_FALSE(logged->error_code.has_value());
    REQUIRE(logged->transport.has_value());
    REQUIRE(*logged->transport == "http");
    REQUIRE(logged->account_id.has_value());
    sqlite3_close(read_db);
    std::filesystem::remove(db_path);
}

TEST_CASE("runtime persists compact upstream error code in request logs", "[server][runtime][proxy][request-log]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));
    sqlite3_close(db);

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 503,
                .body =
                    R"({"error":{"message":"upstream unavailable","type":"server_error","code":"upstream_unavailable"}})",
                .error_code = "upstream_unavailable",
            };
        }
    );
    tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const auto response = tightrope::tests::server::send_raw_http(
        port,
        make_http_post_request(
            "/backend-api/codex/responses/compact",
            {{"Content-Type", "application/json"}},
            R"({"model":"gpt-5.4","input":"persist-error"})"
        )
    );
    REQUIRE(response.find("503 Service Unavailable") != std::string::npos);

    REQUIRE(runtime.stop());

    sqlite3* read_db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &read_db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    REQUIRE(read_db != nullptr);
    const auto logged = latest_request_log(read_db);
    REQUIRE(logged.has_value());
    REQUIRE(logged->path == "/backend-api/codex/responses/compact");
    REQUIRE(logged->method == "POST");
    REQUIRE(logged->status_code == 503);
    REQUIRE(logged->model.has_value());
    REQUIRE(*logged->model == "gpt-5.4");
    REQUIRE(logged->error_code.has_value());
    REQUIRE(*logged->error_code == "upstream_unavailable");
    REQUIRE(logged->transport.has_value());
    REQUIRE(*logged->transport == "http");
    sqlite3_close(read_db);
    std::filesystem::remove(db_path);
}
