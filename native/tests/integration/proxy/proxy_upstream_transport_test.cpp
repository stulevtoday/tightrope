#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <sqlite3.h>

#include "logging/logger.h"
#include "migration/migration_runner.h"
#include "openai/upstream_headers.h"
#include "repositories/account_repo.h"
#include "repositories/settings_repo.h"
#include "repositories/session_repo.h"
#include "session/http_bridge.h"
#include "upstream_transport.h"
#include "controllers/proxy_controller.h"
#include "contracts/fixture_loader.h"
#include "tests/integration/proxy/include/test_support/fake_upstream_transport.h"

namespace {

class FakeUpstreamTransport final : public tightrope::proxy::UpstreamTransport {
public:
    tightrope::proxy::UpstreamExecutionResult next_result{};
    tightrope::proxy::openai::UpstreamRequestPlan last_plan{};
    bool called = false;

    tightrope::proxy::UpstreamExecutionResult execute(const tightrope::proxy::openai::UpstreamRequestPlan& plan)
        override {
        called = true;
        last_plan = plan;
        return next_result;
    }
};

class EnvVarGuard final {
public:
    explicit EnvVarGuard(std::string name) : name_(std::move(name)) {
        if (const char* existing = std::getenv(name_.c_str()); existing != nullptr) {
            had_original_ = true;
            original_ = existing;
        }
    }

    ~EnvVarGuard() {
        if (had_original_) {
            setenv(name_.c_str(), original_.c_str(), 1);
            return;
        }
        unsetenv(name_.c_str());
    }

private:
    std::string name_;
    bool had_original_ = false;
    std::string original_;
};

bool has_event(
    const std::vector<tightrope::core::logging::LogRecord>& observed,
    const std::string_view component,
    const std::string_view event
) {
    return std::any_of(observed.begin(), observed.end(), [&](const auto& record) {
        return record.component == component && record.event == event;
    });
}

std::optional<tightrope::core::logging::LogRecord> find_event(
    const std::vector<tightrope::core::logging::LogRecord>& observed,
    const std::string_view component,
    const std::string_view event
) {
    const auto it = std::find_if(observed.begin(), observed.end(), [&](const auto& record) {
        return record.component == component && record.event == event;
    });
    if (it == observed.end()) {
        return std::nullopt;
    }
    return *it;
}

std::filesystem::path make_temp_proxy_db_path(const std::string_view suffix) {
    const auto path = std::filesystem::temp_directory_path() /
                      std::filesystem::path("tightrope-proxy-upstream-transport-" + std::string(suffix) + ".sqlite3");
    std::filesystem::remove(path);
    return path;
}

void enable_sticky_threads(sqlite3* db) {
    REQUIRE(db != nullptr);
    tightrope::db::DashboardSettingsPatch patch;
    patch.sticky_threads_enabled = true;
    const auto updated = tightrope::db::update_dashboard_settings(db, patch);
    REQUIRE(updated.has_value());
    REQUIRE(updated->sticky_threads_enabled);
}

void set_upstream_stream_transport(sqlite3* db, std::string_view transport) {
    REQUIRE(db != nullptr);
    tightrope::db::DashboardSettingsPatch patch;
    patch.upstream_stream_transport = std::string(transport);
    const auto updated = tightrope::db::update_dashboard_settings(db, patch);
    REQUIRE(updated.has_value());
    REQUIRE(updated->upstream_stream_transport == transport);
}

std::string encode_utf16le_with_bom(const std::string_view utf8_ascii) {
    std::string encoded;
    encoded.reserve(2 + utf8_ascii.size() * 2);
    encoded.push_back(static_cast<char>(0xFF));
    encoded.push_back(static_cast<char>(0xFE));
    for (const auto value : utf8_ascii) {
        encoded.push_back(static_cast<char>(value));
        encoded.push_back('\0');
    }
    return encoded;
}

} // namespace

TEST_CASE("responses JSON path executes upstream plan and returns upstream body", "[proxy][transport]") {
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_upstream","object":"response","status":"completed","output":[]})",
            };
        }
    );
    tightrope::proxy::set_upstream_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"X-OpenAI-Client-Version", "1.2.3"},
        {"X-Forwarded-For", "10.0.0.1"},
    };
    const auto response =
        tightrope::server::controllers::post_proxy_responses_json("/v1/responses", fixture.request.body, inbound);

    REQUIRE(fake->call_count == 1);
    REQUIRE(fake->last_plan.path == "/codex/responses");
    REQUIRE(fake->last_plan.transport == "http-sse");
    REQUIRE(fake->last_plan.headers.find("X-OpenAI-Client-Version") != fake->last_plan.headers.end());
    REQUIRE(fake->last_plan.headers.find("X-Forwarded-For") == fake->last_plan.headers.end());
    REQUIRE(response.status == 200);
    REQUIRE(response.body.find("\"id\":\"resp_upstream\"") != std::string::npos);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("upstream execute supports parallel in-flight plans", "[proxy][transport][concurrency]") {
    constexpr int kWorkers = 2;
    std::atomic<int> active_calls{0};
    std::atomic<int> peak_calls{0};
    std::mutex barrier_mutex;
    std::condition_variable barrier_cv;
    int ready_workers = 0;

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            {
                std::unique_lock<std::mutex> lock(barrier_mutex);
                ++ready_workers;
                if (ready_workers >= kWorkers) {
                    barrier_cv.notify_all();
                }
                barrier_cv.wait(lock, [&] { return ready_workers >= kWorkers; });
            }

            const auto in_flight = active_calls.fetch_add(1) + 1;
            auto peak = peak_calls.load();
            while (in_flight > peak && !peak_calls.compare_exchange_weak(peak, in_flight)) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            active_calls.fetch_sub(1);

            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_parallel","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const tightrope::proxy::openai::UpstreamRequestPlan plan = {
        .method = "POST",
        .path = "/codex/responses",
        .transport = "http-sse",
        .body = R"({"model":"gpt-5","input":"hello"})",
    };

    std::array<tightrope::proxy::UpstreamExecutionResult, kWorkers> results{};
    const auto started_at = std::chrono::steady_clock::now();
    std::thread first([&] { results[0] = tightrope::proxy::execute_upstream_plan(plan); });
    std::thread second([&] { results[1] = tightrope::proxy::execute_upstream_plan(plan); });
    first.join();
    second.join();
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_at).count();

    REQUIRE(results[0].status == 200);
    REQUIRE(results[1].status == 200);
    REQUIRE(peak_calls.load() >= 2);
    REQUIRE(elapsed_ms < 220);
}

TEST_CASE("responses JSON path adds HTTP bridge turn-state header when absent", "[proxy][transport][bridge]") {
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_turn_state","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto response = tightrope::server::controllers::post_proxy_responses_json("/v1/responses", fixture.request.body);

    REQUIRE(response.status == 200);
    const auto turn_state_it = fake->last_plan.headers.find("x-codex-turn-state");
    REQUIRE(turn_state_it != fake->last_plan.headers.end());
    REQUIRE(turn_state_it->second.starts_with("http_turn_"));
}

TEST_CASE(
    "responses JSON path does not auto-inject previous_response_id across requests",
    "[proxy][transport][bridge]"
) {
    const auto bridge_db_path = make_temp_proxy_db_path("bridge-json-continuity");
    EnvVarGuard db_path_guard("TIGHTROPE_DB_PATH");
    REQUIRE(setenv("TIGHTROPE_DB_PATH", bridge_db_path.string().c_str(), 1) == 0);

    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            const auto call_index = observed_plans.size();
            if (call_index == 1) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 200,
                    .body = R"({"id":"resp_bridge_json_1","object":"response","status":"completed","output":[]})",
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_bridge_json_2","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"session_id", "transport-http-bridge-json"},
    };

    const auto first = tightrope::server::controllers::post_proxy_responses_json("/v1/responses", fixture.request.body, inbound);
    const auto second =
        tightrope::server::controllers::post_proxy_responses_json("/v1/responses", fixture.request.body, inbound);

    REQUIRE(first.status == 200);
    REQUIRE(second.status == 200);
    REQUIRE(observed_plans.size() == 2);
    REQUIRE(observed_plans[0].headers.find("x-codex-turn-state") != observed_plans[0].headers.end());
    REQUIRE(observed_plans[0].headers.at("x-codex-turn-state") == "transport-http-bridge-json");
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\"") == std::string::npos);
    REQUIRE(observed_plans[1].body.find("\"previous_response_id\"") == std::string::npos);

    std::filesystem::remove(bridge_db_path);
}

TEST_CASE(
    "responses JSON path does not rehydrate previous_response_id from persistent bridge storage",
    "[proxy][transport][bridge]"
) {
    const auto bridge_db_path = make_temp_proxy_db_path("bridge-persist");
    EnvVarGuard db_path_guard("TIGHTROPE_DB_PATH");
    REQUIRE(setenv("TIGHTROPE_DB_PATH", bridge_db_path.string().c_str(), 1) == 0);

    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            const auto response_id =
                observed_plans.size() == 1 ? std::string("resp_bridge_persist_1") : std::string("resp_bridge_persist_2");
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = std::string(R"({"id":")") + response_id + R"(","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"session_id", "transport-http-bridge-persist"},
    };

    const auto first = tightrope::server::controllers::post_proxy_responses_json("/v1/responses", fixture.request.body, inbound);
    REQUIRE(first.status == 200);
    tightrope::proxy::session::reset_response_bridge_for_tests();
    const auto second =
        tightrope::server::controllers::post_proxy_responses_json("/v1/responses", fixture.request.body, inbound);

    REQUIRE(second.status == 200);
    REQUIRE(observed_plans.size() == 2);
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\"") == std::string::npos);
    REQUIRE(observed_plans[1].body.find("\"previous_response_id\"") == std::string::npos);

    std::filesystem::remove(bridge_db_path);
}

TEST_CASE("responses JSON API key scoping does not rewrite previous_response_id", "[proxy][transport][bridge]") {
    const auto bridge_db_path = make_temp_proxy_db_path("bridge-api-scope");
    EnvVarGuard db_path_guard("TIGHTROPE_DB_PATH");
    REQUIRE(setenv("TIGHTROPE_DB_PATH", bridge_db_path.string().c_str(), 1) == 0);

    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            const auto call_index = observed_plans.size();
            if (call_index == 1) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 200,
                    .body = R"({"id":"resp_scope_a_1","object":"response","status":"completed","output":[]})",
                };
            }
            if (call_index == 2) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 200,
                    .body = R"({"id":"resp_scope_b_1","object":"response","status":"completed","output":[]})",
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_scope_a_2","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");

    const tightrope::proxy::openai::HeaderMap key_a_headers = {
        {"session_id", "transport-http-bridge-scope"},
        {"x-tightrope-api-key-id", "api-key-a"},
    };
    const tightrope::proxy::openai::HeaderMap key_b_headers = {
        {"session_id", "transport-http-bridge-scope"},
        {"x-tightrope-api-key-id", "api-key-b"},
    };

    const auto first = tightrope::server::controllers::post_proxy_responses_json("/v1/responses", fixture.request.body, key_a_headers);
    const auto second = tightrope::server::controllers::post_proxy_responses_json("/v1/responses", fixture.request.body, key_b_headers);
    const auto third = tightrope::server::controllers::post_proxy_responses_json("/v1/responses", fixture.request.body, key_a_headers);

    REQUIRE(first.status == 200);
    REQUIRE(second.status == 200);
    REQUIRE(third.status == 200);
    REQUIRE(observed_plans.size() == 3);
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\"") == std::string::npos);
    REQUIRE(observed_plans[1].body.find("\"previous_response_id\"") == std::string::npos);
    REQUIRE(observed_plans[2].body.find("\"previous_response_id\"") == std::string::npos);
    REQUIRE(observed_plans[0].headers.find("x-tightrope-api-key-id") == observed_plans[0].headers.end());
    REQUIRE(observed_plans[1].headers.find("x-tightrope-api-key-id") == observed_plans[1].headers.end());
    REQUIRE(observed_plans[2].headers.find("x-tightrope-api-key-id") == observed_plans[2].headers.end());

    std::filesystem::remove(bridge_db_path);
}

TEST_CASE(
    "responses JSON continuity prefers turn-state over session_id when both headers are present",
    "[proxy][transport][bridge]"
) {
    const auto bridge_db_path = make_temp_proxy_db_path("bridge-turn-state-precedence");
    EnvVarGuard db_path_guard("TIGHTROPE_DB_PATH");
    REQUIRE(setenv("TIGHTROPE_DB_PATH", bridge_db_path.string().c_str(), 1) == 0);

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_turn_state_precedence","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"session_id", "transport-http-bridge-session"},
        {"x-codex-turn-state", "transport-http-bridge-turn"},
        {"chatgpt-account-id", "acc-turn-state"},
    };

    const auto response =
        tightrope::server::controllers::post_proxy_responses_json("/v1/responses", fixture.request.body, inbound);
    REQUIRE(response.status == 200);

    const std::string followup_payload =
        R"({"model":"gpt-5.4","input":"turn-state precedence","previous_response_id":"resp_turn_state_precedence"})";
    const auto session_only = tightrope::proxy::session::resolve_preferred_account_id_from_previous_response(
        followup_payload,
        {{"session_id", "transport-http-bridge-session"}}
    );
    REQUIRE_FALSE(session_only.has_value());

    const auto same_turn = tightrope::proxy::session::resolve_preferred_account_id_from_previous_response(
        followup_payload,
        {{"session_id", "transport-http-bridge-session"}, {"x-codex-turn-state", "transport-http-bridge-turn"}}
    );
    REQUIRE(same_turn.has_value());
    REQUIRE(*same_turn == "acc-turn-state");

    std::filesystem::remove(bridge_db_path);
}

TEST_CASE("responses JSON path reuses sticky account by prompt cache key", "[proxy][transport][sticky]") {
    const auto sticky_db_file =
        std::filesystem::temp_directory_path() / std::filesystem::path("tightrope-proxy-sticky-transport.sqlite3");
    std::filesystem::remove(sticky_db_file);
    EnvVarGuard db_path_guard("TIGHTROPE_DB_PATH");
    REQUIRE(setenv("TIGHTROPE_DB_PATH", sticky_db_file.string().c_str(), 1) == 0);
    {
        sqlite3* db = nullptr;
        REQUIRE(sqlite3_open_v2(sticky_db_file.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
        REQUIRE(db != nullptr);
        REQUIRE(tightrope::db::run_migrations(db));
        enable_sticky_threads(db);
        sqlite3_close(db);
    }

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_upstream","object":"response","status":"completed","output":[]})",
            };
        }
    );
    tightrope::proxy::set_upstream_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":"sticky test","prompt_cache_key":"transport-sticky-acc-001"})";

    const tightrope::proxy::openai::HeaderMap first_headers = {
        {"chatgpt-account-id", "acc-sticky-001"},
    };

    const auto first =
        tightrope::server::controllers::post_proxy_responses_json("/v1/responses", payload, first_headers);
    REQUIRE(first.status == 200);
    REQUIRE(fake->call_count == 1);
    REQUIRE(fake->last_plan.headers.find("chatgpt-account-id") != fake->last_plan.headers.end());
    REQUIRE(fake->last_plan.headers.at("chatgpt-account-id") == "acc-sticky-001");

    const auto first_call_count = fake->call_count;
    const auto second = tightrope::server::controllers::post_proxy_responses_json("/v1/responses", payload);
    REQUIRE(second.status == 200);
    REQUIRE(fake->call_count == first_call_count + 1);
    REQUIRE(fake->last_plan.headers.find("chatgpt-account-id") != fake->last_plan.headers.end());
    REQUIRE(fake->last_plan.headers.at("chatgpt-account-id") == "acc-sticky-001");

    sqlite3* db = nullptr;
    REQUIRE(
        sqlite3_open_v2(sticky_db_file.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) ==
        SQLITE_OK
    );
    REQUIRE(db != nullptr);
    const auto persisted = tightrope::db::list_proxy_sticky_sessions(db, 10, 0);
    const auto it = std::find_if(persisted.begin(), persisted.end(), [](const auto& row) {
        return row.session_key == "transport-sticky-acc-001";
    });
    REQUIRE(it != persisted.end());
    REQUIRE(it->kind == "prompt_cache");
    sqlite3_close(db);

    tightrope::proxy::reset_upstream_transport();
    std::filesystem::remove(sticky_db_file);
}

TEST_CASE(
    "responses JSON path persists sticky session via turn-state header fallback",
    "[proxy][transport][sticky][bridge]"
) {
    const auto sticky_db_file =
        std::filesystem::temp_directory_path() / std::filesystem::path("tightrope-proxy-sticky-turn-state.sqlite3");
    std::filesystem::remove(sticky_db_file);
    EnvVarGuard db_path_guard("TIGHTROPE_DB_PATH");
    REQUIRE(setenv("TIGHTROPE_DB_PATH", sticky_db_file.string().c_str(), 1) == 0);
    {
        sqlite3* db = nullptr;
        REQUIRE(sqlite3_open_v2(sticky_db_file.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
        REQUIRE(db != nullptr);
        REQUIRE(tightrope::db::run_migrations(db));
        enable_sticky_threads(db);
        sqlite3_close(db);
    }

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_turn_state_sticky","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload = R"({"model":"gpt-5.4","input":"turn-state sticky fallback"})";
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"chatgpt-account-id", "acc-turn-state-sticky"},
    };

    const auto response =
        tightrope::server::controllers::post_proxy_responses_json("/v1/responses", payload, inbound);
    REQUIRE(response.status == 200);

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(sticky_db_file.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    const auto persisted = tightrope::db::list_proxy_sticky_sessions(db, 10, 0);
    REQUIRE_FALSE(persisted.empty());
    REQUIRE(persisted.front().account_id == "acc-turn-state-sticky");
    REQUIRE(persisted.front().session_key.starts_with("http_turn_"));
    REQUIRE(persisted.front().kind == "codex_session");
    sqlite3_close(db);

    std::filesystem::remove(sticky_db_file);
}

TEST_CASE(
    "backend websocket path persists sticky session via session_id key",
    "[proxy][transport][ws][sticky]"
) {
    const auto sticky_db_file =
        std::filesystem::temp_directory_path() / std::filesystem::path("tightrope-proxy-sticky-backend-ws.sqlite3");
    std::filesystem::remove(sticky_db_file);
    EnvVarGuard db_path_guard("TIGHTROPE_DB_PATH");
    REQUIRE(setenv("TIGHTROPE_DB_PATH", sticky_db_file.string().c_str(), 1) == 0);
    {
        sqlite3* db = nullptr;
        REQUIRE(
            sqlite3_open_v2(
                sticky_db_file.string().c_str(),
                &db,
                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                nullptr
            ) == SQLITE_OK
        );
        REQUIRE(db != nullptr);
        REQUIRE(tightrope::db::run_migrations(db));
        enable_sticky_threads(db);
        sqlite3_close(db);
    }

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 101,
                .events = {
                    R"({"type":"response.created","response":{"id":"resp_ws_sticky_created","status":"in_progress"}})",
                    R"({"type":"response.completed","response":{"id":"resp_ws_sticky_created","object":"response","status":"completed","output":[]}})",
                },
                .accepted = true,
                .close_code = 1000,
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload = R"({"model":"gpt-5.4","input":"backend ws sticky create"})";
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"chatgpt-account-id", "acc-backend-ws-sticky"},
        {"session_id", "ws-sticky-session-key"},
    };

    const auto response = tightrope::server::controllers::proxy_responses_websocket(
        "/backend-api/codex/responses",
        payload,
        inbound
    );
    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);

    sqlite3* db = nullptr;
    REQUIRE(
        sqlite3_open_v2(sticky_db_file.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) ==
        SQLITE_OK
    );
    REQUIRE(db != nullptr);
    const auto persisted = tightrope::db::list_proxy_sticky_sessions(db, 20, 0);
    const auto it = std::find_if(persisted.begin(), persisted.end(), [](const auto& row) {
        return row.session_key == "ws-sticky-session-key";
    });
    REQUIRE(it != persisted.end());
    REQUIRE(it->account_id == "acc-backend-ws-sticky");
    REQUIRE(it->kind == "codex_session");
    sqlite3_close(db);

    std::filesystem::remove(sticky_db_file);
}

TEST_CASE(
    "responses JSON path replays account from previous_response_id only within continuity session",
    "[proxy][transport][bridge][account]"
) {
    const auto db_path = make_temp_proxy_db_path("bridge-account-replay");
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::db::OauthAccountUpsert replay_account;
    replay_account.email = "replay@example.com";
    replay_account.provider = "openai";
    replay_account.chatgpt_account_id = "acc-replay";
    replay_account.plan_type = "plus";
    replay_account.access_token_encrypted = "enc-access-replay";
    replay_account.refresh_token_encrypted = "enc-refresh-replay";
    replay_account.id_token_encrypted = "enc-id-replay";
    const auto replay_upsert = tightrope::db::upsert_oauth_account(db, replay_account);
    REQUIRE(replay_upsert.has_value());

    tightrope::db::OauthAccountUpsert fallback_account;
    fallback_account.email = "fallback@example.com";
    fallback_account.provider = "openai";
    fallback_account.chatgpt_account_id = "acc-fallback";
    fallback_account.plan_type = "plus";
    fallback_account.access_token_encrypted = "enc-access-fallback";
    fallback_account.refresh_token_encrypted = "enc-refresh-fallback";
    fallback_account.id_token_encrypted = "enc-id-fallback";
    const auto fallback_upsert = tightrope::db::upsert_oauth_account(db, fallback_account);
    REQUIRE(fallback_upsert.has_value());
    sqlite3_close(db);

    EnvVarGuard db_path_guard("TIGHTROPE_DB_PATH");
    REQUIRE(setenv("TIGHTROPE_DB_PATH", db_path.string().c_str(), 1) == 0);
    EnvVarGuard token_strict_guard("TIGHTROPE_TOKEN_ENCRYPTION_REQUIRE_ENCRYPTED_AT_REST");
    REQUIRE(setenv("TIGHTROPE_TOKEN_ENCRYPTION_REQUIRE_ENCRYPTED_AT_REST", "0", 1) == 0);

    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            if (observed_plans.size() == 1) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 200,
                    .body = R"({"id":"resp_account_replay_1","object":"response","status":"completed","output":[]})",
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_account_replay_2","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const tightrope::proxy::openai::HeaderMap first_headers = {
        {"session_id", "transport-http-bridge-account-replay"},
        {"chatgpt-account-id", "acc-replay"},
    };

    const auto first =
        tightrope::server::controllers::post_proxy_responses_json("/v1/responses", fixture.request.body, first_headers);
    REQUIRE(first.status == 200);

    const std::string second_payload =
        R"({"model":"gpt-5.4","input":"account replay","previous_response_id":"resp_account_replay_1"})";
    const tightrope::proxy::openai::HeaderMap second_headers = {
        {"session_id", "transport-http-bridge-account-replay"},
    };
    const auto scoped_preference =
        tightrope::proxy::session::resolve_preferred_account_id_from_previous_response(second_payload, second_headers);
    REQUIRE(scoped_preference.has_value());
    REQUIRE(*scoped_preference == "acc-replay");
    const auto second =
        tightrope::server::controllers::post_proxy_responses_json("/v1/responses", second_payload, second_headers);

    REQUIRE(second.status == 200);
    REQUIRE(observed_plans.size() == 2);
    REQUIRE(observed_plans[1].headers.find("chatgpt-account-id") != observed_plans[1].headers.end());
    REQUIRE(observed_plans[1].headers.at("chatgpt-account-id") == "acc-replay");

    std::filesystem::remove(db_path);
}

TEST_CASE(
    "responses JSON path retries without previous_response_id after previous_response_not_found",
    "[proxy][transport][guard]"
) {
    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            if (plan.body.find("\"previous_response_id\":\"resp_stale_json_guard\"") != std::string::npos) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 400,
                    .body =
                        R"({"error":{"type":"invalid_request_error","code":"previous_response_not_found","message":"Previous response not found.","param":"previous_response_id"}})",
                    .error_code = "previous_response_not_found",
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_stale_json_guard_recovered","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":"guard json retry","previous_response_id":"resp_stale_json_guard"})";
    const auto response = tightrope::server::controllers::post_proxy_responses_json("/v1/responses", payload);

    REQUIRE(response.status == 200);
    REQUIRE(observed_plans.size() == 2);
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\":\"resp_stale_json_guard\"") != std::string::npos);
    REQUIRE(observed_plans[1].body.find("\"previous_response_id\"") == std::string::npos);
}

TEST_CASE(
    "responses JSON path rebuilds local text context when previous_response_id disappears",
    "[proxy][transport][guard][context]"
) {
    const auto db_path = make_temp_proxy_db_path("local-context-json-recovery");
    EnvVarGuard db_path_guard("TIGHTROPE_DB_PATH");
    REQUIRE(setenv("TIGHTROPE_DB_PATH", db_path.string().c_str(), 1) == 0);

    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            if (observed_plans.size() == 1) {
                REQUIRE(plan.body.find("context turn one") != std::string::npos);
                REQUIRE(plan.body.find("\"previous_response_id\"") == std::string::npos);
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 200,
                    .body =
                        R"({"id":"resp_local_context_one","object":"response","status":"completed","output":[{"type":"reasoning","summary":[]},{"type":"message","role":"assistant","content":[{"type":"output_text","text":"assistant memory one"}]}]})",
                };
            }
            if (observed_plans.size() == 2) {
                REQUIRE(plan.body.find("\"previous_response_id\":\"resp_local_context_one\"") != std::string::npos);
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 400,
                    .body =
                        R"({"error":{"type":"invalid_request_error","code":"previous_response_not_found","message":"Previous response not found.","param":"previous_response_id"}})",
                    .error_code = "previous_response_not_found",
                };
            }

            REQUIRE(plan.body.find("\"previous_response_id\"") == std::string::npos);
            REQUIRE(plan.body.find("context turn one") != std::string::npos);
            REQUIRE(plan.body.find("assistant memory one") != std::string::npos);
            REQUIRE(plan.body.find("context turn two") != std::string::npos);
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_local_context_recovered","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const tightrope::proxy::openai::HeaderMap headers = {{"session_id", "local-context-json-session"}};
    const auto first = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"context turn one"})",
        headers
    );
    REQUIRE(first.status == 200);

    const auto second = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"context turn two","previous_response_id":"resp_local_context_one"})",
        headers
    );
    REQUIRE(second.status == 200);
    REQUIRE(observed_plans.size() == 3);

    std::filesystem::remove(db_path);
}

TEST_CASE(
    "backend responses JSON path strips unknown previous_response_id before upstream",
    "[proxy][transport][guard][backend]"
) {
    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_backend_json_guard_stripped","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":"guard backend json retry","previous_response_id":"resp_backend_json_guard_prev"})";
    const auto response =
        tightrope::server::controllers::post_proxy_responses_json("/backend-api/codex/responses", payload);

    REQUIRE(response.status == 200);
    REQUIRE(observed_plans.size() == 1);
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\"") == std::string::npos);
    REQUIRE(response.body.find("\"resp_backend_json_guard_stripped\"") != std::string::npos);
}

TEST_CASE(
    "responses JSON path does not strip previous_response_id for function_call_output payloads",
    "[proxy][transport][guard]"
) {
    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            if (observed_plans.size() == 1) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 400,
                    .body =
                        R"({"error":{"type":"invalid_request_error","code":"previous_response_not_found","message":"Previous response not found.","param":"previous_response_id"}})",
                    .error_code = "previous_response_not_found",
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"id":"resp_guard_json_tool_output_unexpected","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":[{"type":"function_call_output","call_id":"call_guard_json_tool","output":"ok"}],"previous_response_id":"resp_stale_json_guard_tool"})";
    const auto response = tightrope::server::controllers::post_proxy_responses_json("/v1/responses", payload);

    REQUIRE(response.status == 400);
    REQUIRE(observed_plans.size() == 1);
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\":\"resp_stale_json_guard_tool\"") != std::string::npos);
}

TEST_CASE(
    "backend responses JSON path preflights bridged previous_response_id for function_call_output payloads",
    "[proxy][transport][guard][backend][bridge]"
) {
    const auto bridge_db_path = make_temp_proxy_db_path("bridge-json-tool-previous-rewrite");
    sqlite3* db = nullptr;
    REQUIRE(
        sqlite3_open_v2(bridge_db_path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) ==
        SQLITE_OK
    );
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::ensure_proxy_response_continuity_schema(db));
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()
    )
                            .count();
    REQUIRE(tightrope::db::upsert_proxy_response_continuity(
        db,
        "transport-http-bridge-json-tool",
        "",
        "resp_current_json_tool",
        "acc-json-tool",
        now_ms,
        120000
    ));
    sqlite3_close(db);

    EnvVarGuard db_path_guard("TIGHTROPE_DB_PATH");
    REQUIRE(setenv("TIGHTROPE_DB_PATH", bridge_db_path.string().c_str(), 1) == 0);

    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            const bool has_current = plan.body.find("\"previous_response_id\":\"resp_current_json_tool\"") !=
                                     std::string::npos;
            if (has_current) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 200,
                    .body = R"({"id":"resp_guard_json_tool_retried","object":"response","status":"completed","output":[]})",
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 400,
                .body =
                    R"({"error":{"type":"invalid_request_error","code":"previous_response_not_found","message":"Previous response not found.","param":"previous_response_id"}})",
                .error_code = "previous_response_not_found",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":[{"type":"function_call_output","call_id":"call_guard_json_tool_backend","output":"ok"}],"previous_response_id":"resp_stale_json_tool_rewrite"})";
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"session_id", "transport-http-bridge-json-tool"},
    };
    const auto response =
        tightrope::server::controllers::post_proxy_responses_json("/backend-api/codex/responses", payload, inbound);

    REQUIRE(response.status == 200);
    REQUIRE(observed_plans.size() == 1);
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\":\"resp_current_json_tool\"") != std::string::npos);
    REQUIRE(observed_plans[0].body.find("\"resp_stale_json_tool_rewrite\"") == std::string::npos);
    REQUIRE(response.body.find("\"resp_guard_json_tool_retried\"") != std::string::npos);

    std::filesystem::remove(bridge_db_path);
}

TEST_CASE(
    "backend responses JSON bridge rewrite preserves continuation account context for function_call_output",
    "[proxy][transport][guard][backend][bridge][account]"
) {
    const auto bridge_db_path = make_temp_proxy_db_path("bridge-json-tool-account-continuity");
    sqlite3* db = nullptr;
    REQUIRE(
        sqlite3_open_v2(bridge_db_path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) ==
        SQLITE_OK
    );
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));

    tightrope::db::OauthAccountUpsert account;
    account.email = "json-tool-account@example.com";
    account.provider = "openai";
    account.chatgpt_account_id = "acc-json-tool-cont";
    account.plan_type = "plus";
    account.access_token_encrypted = "enc-access-json-tool-cont";
    account.refresh_token_encrypted = "enc-refresh-json-tool-cont";
    account.id_token_encrypted = "enc-id-json-tool-cont";
    const auto account_upsert = tightrope::db::upsert_oauth_account(db, account);
    REQUIRE(account_upsert.has_value());

    REQUIRE(tightrope::db::ensure_proxy_response_continuity_schema(db));
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()
    )
                            .count();
    REQUIRE(tightrope::db::upsert_proxy_response_continuity(
        db,
        "transport-http-bridge-json-tool-account",
        "",
        "resp_current_json_tool_account",
        "acc-json-tool-cont",
        now_ms,
        120000
    ));
    sqlite3_close(db);

    EnvVarGuard db_path_guard("TIGHTROPE_DB_PATH");
    REQUIRE(setenv("TIGHTROPE_DB_PATH", bridge_db_path.string().c_str(), 1) == 0);
    EnvVarGuard token_strict_guard("TIGHTROPE_TOKEN_ENCRYPTION_REQUIRE_ENCRYPTED_AT_REST");
    REQUIRE(setenv("TIGHTROPE_TOKEN_ENCRYPTION_REQUIRE_ENCRYPTED_AT_REST", "0", 1) == 0);

    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            const bool has_current = plan.body.find("\"previous_response_id\":\"resp_current_json_tool_account\"") !=
                                     std::string::npos;
            if (has_current) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 200,
                    .body = R"({"id":"resp_guard_json_tool_account_retried","object":"response","status":"completed","output":[]})",
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 400,
                .body =
                    R"({"error":{"type":"invalid_request_error","code":"previous_response_not_found","message":"Previous response not found.","param":"previous_response_id"}})",
                .error_code = "previous_response_not_found",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":[{"type":"function_call_output","call_id":"call_guard_json_tool_account","output":"ok"}],"previous_response_id":"resp_stale_json_tool_account"})";
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"session_id", "transport-http-bridge-json-tool-account"},
    };
    const auto response =
        tightrope::server::controllers::post_proxy_responses_json("/backend-api/codex/responses", payload, inbound);

    REQUIRE(response.status == 200);
    REQUIRE(observed_plans.size() == 1);
    REQUIRE(
        observed_plans[0].body.find("\"previous_response_id\":\"resp_current_json_tool_account\"") != std::string::npos
    );
    REQUIRE(observed_plans[0].headers.find("chatgpt-account-id") != observed_plans[0].headers.end());
    REQUIRE(observed_plans[0].headers.at("chatgpt-account-id") == "acc-json-tool-cont");
    REQUIRE(response.body.find("\"resp_guard_json_tool_account_retried\"") != std::string::npos);

    std::filesystem::remove(bridge_db_path);
}

TEST_CASE(
    "responses SSE path does not auto-inject previous_response_id across requests",
    "[proxy][transport][bridge]"
) {
    const auto bridge_db_path = make_temp_proxy_db_path("bridge-sse-continuity");
    EnvVarGuard db_path_guard("TIGHTROPE_DB_PATH");
    REQUIRE(setenv("TIGHTROPE_DB_PATH", bridge_db_path.string().c_str(), 1) == 0);

    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            const auto call_index = observed_plans.size();
            const auto response_id =
                call_index == 1 ? std::string("resp_bridge_sse_1") : std::string("resp_bridge_sse_2");
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .events = {
                    std::string(R"({"type":"response.created","response":{"id":")") + response_id +
                        R"(","status":"in_progress"}})",
                    std::string(R"({"type":"response.completed","response":{"id":")") + response_id +
                        R"(","object":"response","status":"completed","output":[]}})",
                },
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload = R"({"model":"gpt-4o","input":"bridge sse continuity"})";
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"session_id", "transport-http-bridge-sse"},
    };

    const auto first = tightrope::server::controllers::post_proxy_responses_sse("/v1/responses", payload, inbound);
    const auto second =
        tightrope::server::controllers::post_proxy_responses_sse("/v1/responses", payload, inbound);

    REQUIRE(first.status == 200);
    REQUIRE(second.status == 200);
    REQUIRE(observed_plans.size() == 2);
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\"") == std::string::npos);
    REQUIRE(observed_plans[1].body.find("\"previous_response_id\"") == std::string::npos);

    std::filesystem::remove(bridge_db_path);
}

TEST_CASE("responses SSE path returns upstream events when available", "[proxy][transport]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 200,
        .body = "",
        .events = {R"({"event":"a"})", R"({"event":"b"})"},
    };
    tightrope::proxy::set_upstream_transport(fake);

    const auto response = tightrope::server::controllers::post_proxy_responses_sse(
        "/v1/responses",
        R"({"model":"gpt-4o","input":"ping"})"
    );

    REQUIRE(fake->called);
    REQUIRE(response.status == 200);
    REQUIRE(response.events == std::vector<std::string>{R"({"event":"a"})", R"({"event":"b"})"});

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("responses SSE path preserves upstream error message for exhausted account failures", "[proxy][transport][sse]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 429,
        .body = R"({"error":{"type":"invalid_request_error","code":"rate_limit_exceeded","message":"Usage limit has been reached","param":"input"}})",
        .error_code = "rate_limit_exceeded",
    };
    tightrope::proxy::set_upstream_transport(fake);

    const auto response = tightrope::server::controllers::post_proxy_responses_sse(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"quota"})"
    );

    REQUIRE(fake->called);
    REQUIRE(response.status == 429);
    REQUIRE(response.events.size() == 1);
    REQUIRE(response.events.front().find("\"code\":\"rate_limit_exceeded\"") != std::string::npos);
    REQUIRE(response.events.front().find("Usage limit has been reached") != std::string::npos);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE(
    "responses SSE path retries without previous_response_id after previous_response_not_found",
    "[proxy][transport][guard][sse]"
) {
    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            if (plan.body.find("\"previous_response_id\":\"resp_stale_sse_guard\"") != std::string::npos) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 400,
                    .body =
                        R"({"error":{"type":"invalid_request_error","code":"previous_response_not_found","message":"Previous response not found.","param":"previous_response_id"}})",
                    .error_code = "previous_response_not_found",
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .events = {
                    R"({"type":"response.created","response":{"id":"resp_guard_sse_recovered","status":"in_progress"}})",
                    R"({"type":"response.completed","response":{"id":"resp_guard_sse_recovered","object":"response","status":"completed","output":[]}})",
                },
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":"guard sse retry","previous_response_id":"resp_stale_sse_guard"})";
    const auto response = tightrope::server::controllers::post_proxy_responses_sse("/v1/responses", payload);

    REQUIRE(response.status == 200);
    REQUIRE(observed_plans.size() == 2);
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\":\"resp_stale_sse_guard\"") != std::string::npos);
    REQUIRE(observed_plans[1].body.find("\"previous_response_id\"") == std::string::npos);
    REQUIRE(response.events.size() == 2);
    REQUIRE(response.events[1].find("\"resp_guard_sse_recovered\"") != std::string::npos);
}

TEST_CASE(
    "backend responses SSE path strips unknown previous_response_id before upstream",
    "[proxy][transport][guard][sse][backend]"
) {
    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .events = {
                    R"({"type":"response.created","response":{"id":"resp_backend_sse_guard_stripped","status":"in_progress"}})",
                    R"({"type":"response.completed","response":{"id":"resp_backend_sse_guard_stripped","object":"response","status":"completed","output":[]}})",
                },
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":"guard backend sse retry","previous_response_id":"resp_backend_sse_guard_prev"})";
    const auto response =
        tightrope::server::controllers::post_proxy_responses_sse("/backend-api/codex/responses", payload);

    REQUIRE(response.status == 200);
    REQUIRE(observed_plans.size() == 1);
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\"") == std::string::npos);
    REQUIRE(response.events.size() == 2);
    REQUIRE(response.events[1].find("\"resp_backend_sse_guard_stripped\"") != std::string::npos);
}

TEST_CASE(
    "responses SSE path does not strip previous_response_id for function_call_output payloads",
    "[proxy][transport][guard][sse]"
) {
    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            if (observed_plans.size() == 1) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 400,
                    .body =
                        R"({"error":{"type":"invalid_request_error","code":"previous_response_not_found","message":"Previous response not found.","param":"previous_response_id"}})",
                    .error_code = "previous_response_not_found",
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .events = {
                    R"({"type":"response.created","response":{"id":"resp_guard_sse_tool_output_unexpected","status":"in_progress"}})",
                    R"({"type":"response.completed","response":{"id":"resp_guard_sse_tool_output_unexpected","object":"response","status":"completed","output":[]}})",
                },
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":[{"type":"function_call_output","call_id":"call_guard_sse_tool","output":"ok"}],"previous_response_id":"resp_stale_sse_guard_tool"})";
    const auto response = tightrope::server::controllers::post_proxy_responses_sse("/v1/responses", payload);

    REQUIRE(response.status == 400);
    REQUIRE(observed_plans.size() == 1);
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\":\"resp_stale_sse_guard_tool\"") != std::string::npos);
}

TEST_CASE(
    "backend responses SSE path preflights bridged previous_response_id for function_call_output payloads",
    "[proxy][transport][guard][sse][backend][bridge]"
) {
    const auto bridge_db_path = make_temp_proxy_db_path("bridge-sse-tool-previous-rewrite");
    sqlite3* db = nullptr;
    REQUIRE(
        sqlite3_open_v2(bridge_db_path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) ==
        SQLITE_OK
    );
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::ensure_proxy_response_continuity_schema(db));
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()
    )
                            .count();
    REQUIRE(tightrope::db::upsert_proxy_response_continuity(
        db,
        "transport-http-bridge-sse-tool",
        "",
        "resp_current_sse_tool",
        "acc-sse-tool",
        now_ms,
        120000
    ));
    sqlite3_close(db);

    EnvVarGuard db_path_guard("TIGHTROPE_DB_PATH");
    REQUIRE(setenv("TIGHTROPE_DB_PATH", bridge_db_path.string().c_str(), 1) == 0);

    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            const bool has_current = plan.body.find("\"previous_response_id\":\"resp_current_sse_tool\"") !=
                                     std::string::npos;
            if (has_current) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 200,
                    .events = {
                        R"({"type":"response.created","response":{"id":"resp_guard_sse_tool_retried","status":"in_progress"}})",
                        R"({"type":"response.completed","response":{"id":"resp_guard_sse_tool_retried","object":"response","status":"completed","output":[]}})",
                    },
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 400,
                .body =
                    R"({"error":{"type":"invalid_request_error","code":"previous_response_not_found","message":"Previous response not found.","param":"previous_response_id"}})",
                .error_code = "previous_response_not_found",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":[{"type":"function_call_output","call_id":"call_guard_sse_tool_backend","output":"ok"}],"previous_response_id":"resp_stale_sse_tool_rewrite"})";
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"session_id", "transport-http-bridge-sse-tool"},
    };
    const auto response =
        tightrope::server::controllers::post_proxy_responses_sse("/backend-api/codex/responses", payload, inbound);

    REQUIRE(response.status == 200);
    REQUIRE(observed_plans.size() == 1);
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\":\"resp_current_sse_tool\"") != std::string::npos);
    REQUIRE(observed_plans[0].body.find("\"resp_stale_sse_tool_rewrite\"") == std::string::npos);
    REQUIRE(response.events.size() == 2);
    REQUIRE(response.events[1].find("\"resp_guard_sse_tool_retried\"") != std::string::npos);

    std::filesystem::remove(bridge_db_path);
}

TEST_CASE("responses SSE path resolves websocket upstream transport for native codex headers", "[proxy][transport]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 200,
        .body = "",
        .events = {R"({"contract":"proxy-streaming-v1","event_type":"response.created"})"},
    };
    tightrope::proxy::set_upstream_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"Originator", "codex_cli_rs"},
        {"Connection", "keep-alive, Upgrade"},
        {"Sec-WebSocket-Key", "abc"},
    };
    const auto response =
        tightrope::server::controllers::post_proxy_responses_sse("/v1/responses", fixture.request.body, inbound);

    REQUIRE(fake->called);
    REQUIRE(fake->last_plan.transport == "websocket");
    REQUIRE(fake->last_plan.path == "/codex/responses");
    REQUIRE(fake->last_plan.body.find("\"type\":\"response.create\"") != std::string::npos);
    REQUIRE(fake->last_plan.headers.find("Connection") == fake->last_plan.headers.end());
    REQUIRE(response.status == 200);
    REQUIRE(response.events == std::vector<std::string>{R"({"contract":"proxy-streaming-v1","event_type":"response.created"})"});

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("responses SSE path honors configured upstream stream transport from settings", "[proxy][transport]") {
    const auto db_path = make_temp_proxy_db_path("sse-configured-transport");
    EnvVarGuard db_path_guard("TIGHTROPE_DB_PATH");
    REQUIRE(setenv("TIGHTROPE_DB_PATH", db_path.string().c_str(), 1) == 0);

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));
    set_upstream_stream_transport(db, "websocket");
    sqlite3_close(db);

    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 200,
        .body = "",
        .events = {R"({"contract":"proxy-streaming-v1","event_type":"response.created"})"},
    };
    tightrope::proxy::set_upstream_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto response = tightrope::server::controllers::post_proxy_responses_sse("/v1/responses", fixture.request.body);

    REQUIRE(fake->called);
    REQUIRE(fake->last_plan.transport == "websocket");
    REQUIRE(response.status == 200);

    tightrope::proxy::reset_upstream_transport();
    std::filesystem::remove(db_path);
}

TEST_CASE("backend SSE route preserves upstream websocket session", "[proxy][transport][sse][guard]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 200,
        .body = "",
        .events = {R"({"type":"response.created","response":{"id":"resp_backend_sse_preserve","status":"in_progress"}})",
                   R"({"type":"response.completed","response":{"id":"resp_backend_sse_preserve","object":"response","status":"completed","output":[]}})"},
    };
    tightrope::proxy::set_upstream_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"Originator", "codex_cli_rs"},
        {"Connection", "keep-alive, Upgrade"},
        {"Sec-WebSocket-Key", "abc"},
    };

    const auto backend =
        tightrope::server::controllers::post_proxy_responses_sse("/backend-api/codex/responses", fixture.request.body, inbound);
    REQUIRE(backend.status == 200);
    REQUIRE(fake->last_plan.transport == "websocket");
    REQUIRE(fake->last_plan.preserve_upstream_websocket_session);

    const auto openai = tightrope::server::controllers::post_proxy_responses_sse("/v1/responses", fixture.request.body, inbound);
    REQUIRE(openai.status == 200);
    REQUIRE(fake->last_plan.transport == "websocket");
    REQUIRE_FALSE(fake->last_plan.preserve_upstream_websocket_session);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("backend SSE route accepts function_call_output payloads with escaped control bytes", "[proxy][transport][sse][guard]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 200,
        .body = "",
        .events = {
            R"({"type":"response.created","response":{"id":"resp_backend_sse_ctrl","status":"in_progress"}})",
            R"({"type":"response.completed","response":{"id":"resp_backend_sse_ctrl","object":"response","status":"completed","output":[]}})",
        },
    };
    tightrope::proxy::set_upstream_transport(fake);

    const tightrope::proxy::openai::HeaderMap inbound = {
        {"Originator", "codex_cli_rs"},
        {"Connection", "keep-alive, Upgrade"},
        {"Sec-WebSocket-Key", "abc"},
    };
    const std::string payload =
        R"({"model":"gpt-5.4","input":[{"type":"function_call_output","call_id":"call_ctrl","output":"ansi \u001b[31mred\u001b[0m"}],"stream":true})";

    const auto response =
        tightrope::server::controllers::post_proxy_responses_sse("/backend-api/codex/responses", payload, inbound);

    REQUIRE(fake->called);
    REQUIRE(fake->last_plan.transport == "websocket");
    REQUIRE(fake->last_plan.body.find("\"type\":\"response.create\"") != std::string::npos);
    REQUIRE(fake->last_plan.body.find("\"call_id\":\"call_ctrl\"") != std::string::npos);
    REQUIRE(response.status == 200);
    REQUIRE(response.events.size() == 2);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("responses SSE fallback emits stream_incomplete failure envelope", "[proxy][transport]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 200,
        .body = "",
        .events = {},
    };
    tightrope::proxy::set_upstream_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto response =
        tightrope::server::controllers::post_proxy_responses_sse("/v1/responses", fixture.request.body);

    REQUIRE(fake->called);
    REQUIRE(response.status == 502);
    REQUIRE(response.events.size() == 1);
    REQUIRE(response.events[0].find("\"type\":\"response.failed\"") != std::string::npos);
    REQUIRE(response.events[0].find("\"code\":\"stream_incomplete\"") != std::string::npos);
    REQUIRE(response.events[0].find("\"message\":\"Upstream stream was incomplete\"") != std::string::npos);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("responses SSE retries stream_incomplete upstream results internally", "[proxy][transport]") {
    std::size_t attempt = 0;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&attempt](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            ++attempt;
            if (attempt == 1) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 101,
                    .events = {R"({"type":"response.output_text.delta","delta":"partial"})"},
                    .accepted = true,
                    .close_code = 1000,
                    .error_code = "stream_incomplete",
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 101,
                .events = {
                    R"({"type":"response.created","response":{"id":"resp_retry_complete","status":"in_progress"}})",
                    R"({"type":"response.completed","response":{"id":"resp_retry_complete","object":"response","status":"completed","output":[]}})",
                },
                .accepted = true,
                .close_code = 1000,
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const tightrope::proxy::openai::HeaderMap inbound = {
        {"Originator", "codex_cli_rs"},
        {"Connection", "keep-alive, Upgrade"},
        {"Sec-WebSocket-Key", "retry"},
    };
    const std::string payload = R"({"model":"gpt-5.4","input":"retry stream incomplete","stream":true})";
    const auto response =
        tightrope::server::controllers::post_proxy_responses_sse("/backend-api/codex/responses", payload, inbound);

    REQUIRE(fake->call_count == 2);
    REQUIRE(response.status == 200);
    REQUIRE(response.events.size() == 2);
    REQUIRE(response.events[0].find("resp_retry_complete") != std::string::npos);
    REQUIRE(response.events[1].find("resp_retry_complete") != std::string::npos);
}

TEST_CASE("websocket proxy path executes websocket request plan and passes through events", "[proxy][transport][ws]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 200,
        .body = "",
        .events = {R"({"type":"response.text.delta","delta":"hello"})"},
    };
    tightrope::proxy::set_upstream_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"Connection", "keep-alive, Upgrade"},
        {"Sec-WebSocket-Key", "abc"},
        {"Originator", "codex_cli_rs"},
    };
    const auto response =
        tightrope::server::controllers::proxy_responses_websocket("/v1/responses", fixture.request.body, inbound);

    REQUIRE(fake->called);
    REQUIRE(fake->last_plan.path == "/codex/responses");
    REQUIRE(fake->last_plan.transport == "websocket");
    REQUIRE(fake->last_plan.body.find("\"type\":\"response.create\"") != std::string::npos);
    REQUIRE(fake->last_plan.headers.find("Connection") == fake->last_plan.headers.end());
    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(response.close_code == 1000);
    REQUIRE(response.frames.size() == 1);
    REQUIRE(response.frames.front().find("response.text.delta") != std::string::npos);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE(
    "responses websocket path does not auto-inject previous_response_id from bridge continuity",
    "[proxy][transport][ws][bridge]"
) {
    const auto bridge_db_path = make_temp_proxy_db_path("bridge-ws-continuity");
    EnvVarGuard db_path_guard("TIGHTROPE_DB_PATH");
    REQUIRE(setenv("TIGHTROPE_DB_PATH", bridge_db_path.string().c_str(), 1) == 0);

    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            const auto call_index = observed_plans.size();
            const auto response_id = call_index == 1 ? std::string("resp_bridge_ws_1") : std::string("resp_bridge_ws_2");
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .events = {
                    std::string(R"({"type":"response.created","response":{"id":")") + response_id +
                        R"(","status":"in_progress"}})",
                    std::string(R"({"type":"response.completed","response":{"id":")") + response_id +
                        R"(","object":"response","status":"completed","output":[]}})",
                },
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"session_id", "transport-http-bridge-ws"},
    };

    const auto first =
        tightrope::server::controllers::proxy_responses_websocket("/v1/responses", fixture.request.body, inbound);
    const auto second =
        tightrope::server::controllers::proxy_responses_websocket("/v1/responses", fixture.request.body, inbound);

    REQUIRE(first.status == 101);
    REQUIRE(first.accepted);
    REQUIRE(second.status == 101);
    REQUIRE(second.accepted);
    REQUIRE(observed_plans.size() == 2);
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\"") == std::string::npos);
    REQUIRE(observed_plans[1].body.find("\"previous_response_id\"") == std::string::npos);

    std::filesystem::remove(bridge_db_path);
}

TEST_CASE(
    "responses websocket path does not use previous_response account outside continuity session",
    "[proxy][transport][ws][bridge][account]"
) {
    const auto db_path = make_temp_proxy_db_path("bridge-ws-account-precedence");
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));
    enable_sticky_threads(db);

    tightrope::db::OauthAccountUpsert replay_account;
    replay_account.email = "ws-replay@example.com";
    replay_account.provider = "openai";
    replay_account.chatgpt_account_id = "acc-ws-replay";
    replay_account.plan_type = "plus";
    replay_account.access_token_encrypted = "enc-access-ws-replay";
    replay_account.refresh_token_encrypted = "enc-refresh-ws-replay";
    replay_account.id_token_encrypted = "enc-id-ws-replay";
    REQUIRE(tightrope::db::upsert_oauth_account(db, replay_account).has_value());

    tightrope::db::OauthAccountUpsert fallback_account;
    fallback_account.email = "ws-fallback@example.com";
    fallback_account.provider = "openai";
    fallback_account.chatgpt_account_id = "acc-ws-fallback";
    fallback_account.plan_type = "plus";
    fallback_account.access_token_encrypted = "enc-access-ws-fallback";
    fallback_account.refresh_token_encrypted = "enc-refresh-ws-fallback";
    fallback_account.id_token_encrypted = "enc-id-ws-fallback";
    REQUIRE(tightrope::db::upsert_oauth_account(db, fallback_account).has_value());

    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()
    )
                            .count();
    REQUIRE(tightrope::db::ensure_proxy_sticky_session_schema(db));
    REQUIRE(tightrope::db::upsert_proxy_sticky_session(
        db,
        "ws-sticky-key",
        "acc-ws-fallback",
        now_ms,
        /*ttl_ms=*/60000
    ));
    REQUIRE(tightrope::db::ensure_proxy_response_continuity_schema(db));
    REQUIRE(tightrope::db::upsert_proxy_response_continuity(
        db,
        "ws-turn-state",
        "",
        "resp_ws_previous_conflict",
        "acc-ws-replay",
        now_ms,
        /*ttl_ms=*/60000
    ));
    sqlite3_close(db);

    EnvVarGuard db_path_guard("TIGHTROPE_DB_PATH");
    REQUIRE(setenv("TIGHTROPE_DB_PATH", db_path.string().c_str(), 1) == 0);
    EnvVarGuard token_strict_guard("TIGHTROPE_TOKEN_ENCRYPTION_REQUIRE_ENCRYPTED_AT_REST");
    REQUIRE(setenv("TIGHTROPE_TOKEN_ENCRYPTION_REQUIRE_ENCRYPTED_AT_REST", "0", 1) == 0);

    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 101,
                .events = {
                    R"({"type":"response.created","response":{"id":"resp_ws_account_precedence","status":"in_progress"}})",
                    R"({"type":"response.completed","response":{"id":"resp_ws_account_precedence","object":"response","status":"completed","output":[]}})",
                },
                .accepted = true,
                .close_code = 1000,
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload = R"({"model":"gpt-5.4","input":"ws precedence","prompt_cache_key":"ws-sticky-key","previous_response_id":"resp_ws_previous_conflict"})";
    const auto preferred_account_id = tightrope::proxy::session::resolve_preferred_account_id_from_previous_response(payload, {});
    REQUIRE_FALSE(preferred_account_id.has_value());
    const auto response = tightrope::server::controllers::proxy_responses_websocket("/v1/responses", payload);

    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(observed_plans.size() == 1);
    REQUIRE(observed_plans[0].headers.find("chatgpt-account-id") != observed_plans[0].headers.end());
    REQUIRE(observed_plans[0].headers.at("chatgpt-account-id") == "acc-ws-fallback");

    std::filesystem::remove(db_path);
}

TEST_CASE(
    "backend websocket path strips previous_response outside continuity session before sticky routing",
    "[proxy][transport][ws][bridge][account]"
) {
    const auto db_path = make_temp_proxy_db_path("bridge-ws-account-precedence-backend");
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::run_migrations(db));
    enable_sticky_threads(db);

    tightrope::db::OauthAccountUpsert replay_account;
    replay_account.email = "ws-backend-replay@example.com";
    replay_account.provider = "openai";
    replay_account.chatgpt_account_id = "acc-ws-backend-replay";
    replay_account.plan_type = "plus";
    replay_account.access_token_encrypted = "enc-access-ws-backend-replay";
    replay_account.refresh_token_encrypted = "enc-refresh-ws-backend-replay";
    replay_account.id_token_encrypted = "enc-id-ws-backend-replay";
    REQUIRE(tightrope::db::upsert_oauth_account(db, replay_account).has_value());

    tightrope::db::OauthAccountUpsert fallback_account;
    fallback_account.email = "ws-backend-fallback@example.com";
    fallback_account.provider = "openai";
    fallback_account.chatgpt_account_id = "acc-ws-backend-fallback";
    fallback_account.plan_type = "plus";
    fallback_account.access_token_encrypted = "enc-access-ws-backend-fallback";
    fallback_account.refresh_token_encrypted = "enc-refresh-ws-backend-fallback";
    fallback_account.id_token_encrypted = "enc-id-ws-backend-fallback";
    REQUIRE(tightrope::db::upsert_oauth_account(db, fallback_account).has_value());

    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()
    )
                            .count();
    REQUIRE(tightrope::db::ensure_proxy_sticky_session_schema(db));
    REQUIRE(tightrope::db::upsert_proxy_sticky_session(
        db,
        "ws-backend-sticky-key",
        "acc-ws-backend-fallback",
        now_ms,
        /*ttl_ms=*/60000
    ));
    REQUIRE(tightrope::db::ensure_proxy_response_continuity_schema(db));
    REQUIRE(tightrope::db::upsert_proxy_response_continuity(
        db,
        "ws-backend-turn-state",
        "api-key-a",
        "resp_ws_backend_previous_conflict",
        "acc-ws-backend-replay",
        now_ms,
        /*ttl_ms=*/60000
    ));
    sqlite3_close(db);

    EnvVarGuard db_path_guard("TIGHTROPE_DB_PATH");
    REQUIRE(setenv("TIGHTROPE_DB_PATH", db_path.string().c_str(), 1) == 0);
    EnvVarGuard token_strict_guard("TIGHTROPE_TOKEN_ENCRYPTION_REQUIRE_ENCRYPTED_AT_REST");
    REQUIRE(setenv("TIGHTROPE_TOKEN_ENCRYPTION_REQUIRE_ENCRYPTED_AT_REST", "0", 1) == 0);

    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 101,
                .events = {
                    R"({"type":"response.created","response":{"id":"resp_ws_backend_account_precedence","status":"in_progress"}})",
                    R"({"type":"response.completed","response":{"id":"resp_ws_backend_account_precedence","object":"response","status":"completed","output":[]}})",
                },
                .accepted = true,
                .close_code = 1000,
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload = R"({"model":"gpt-5.4","input":"ws backend precedence","prompt_cache_key":"ws-backend-sticky-key","previous_response_id":"resp_ws_backend_previous_conflict"})";
    const auto preferred_account_id = tightrope::proxy::session::resolve_preferred_account_id_from_previous_response(payload, {});
    REQUIRE_FALSE(preferred_account_id.has_value());
    const auto response =
        tightrope::server::controllers::proxy_responses_websocket("/backend-api/codex/responses", payload);

    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(observed_plans.size() == 1);
    REQUIRE(observed_plans[0].headers.find("chatgpt-account-id") != observed_plans[0].headers.end());
    REQUIRE(observed_plans[0].headers.at("chatgpt-account-id") == "acc-ws-backend-fallback");
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\"") == std::string::npos);

    std::filesystem::remove(db_path);
}

TEST_CASE("responses websocket path preserves upstream event ordering", "[proxy][transport][ws][parity]") {
    const std::vector<std::string> upstream_events = {
        R"({"type":"response.created","response":{"id":"resp_ws_order","status":"in_progress"}})",
        R"({"type":"response.output_text.delta","delta":"hello"})",
        R"({"type":"response.output_item.added","output_index":0,"item":{"type":"function_call","call_id":"call_ws_order","name":"tool.search","arguments":"{}"}})",
        R"({"type":"response.output_item.done","output_index":0,"item":{"type":"function_call","call_id":"call_ws_order","name":"tool.search","arguments":"{}"}})",
        R"({"type":"response.completed","response":{"id":"resp_ws_order","object":"response","status":"completed","output":[]}})",
    };

    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [upstream_events](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 101,
                .events = upstream_events,
                .accepted = true,
                .close_code = 1000,
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto response =
        tightrope::server::controllers::proxy_responses_websocket("/backend-api/codex/responses", fixture.request.body);

    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(response.frames == upstream_events);
}

TEST_CASE(
    "responses websocket path preserves function_call_output call_id and output payload shape",
    "[proxy][transport][ws][parity]"
) {
    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 101,
                .events = {
                    R"({"type":"response.created","response":{"id":"resp_ws_tool_parity","status":"in_progress"}})",
                    R"({"type":"response.completed","response":{"id":"resp_ws_tool_parity","object":"response","status":"completed","output":[]}})",
                },
                .accepted = true,
                .close_code = 1000,
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"type":"response.create","response":{"model":"gpt-5.4","input":[{"type":"function_call_output","call_id":"call_ws_tool_parity","output":[{"type":"input_text","text":"ok"}]}],"tools":[{"type":"function","name":"tool.search","parameters":{"type":"object","properties":{}}}]}})";
    const auto response =
        tightrope::server::controllers::proxy_responses_websocket("/backend-api/codex/responses", payload);

    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(observed_plans.size() == 1);
    REQUIRE(observed_plans[0].body.find("\"type\":\"response.create\"") != std::string::npos);
    REQUIRE(observed_plans[0].body.find("\"type\":\"function_call_output\"") != std::string::npos);
    REQUIRE(observed_plans[0].body.find("\"call_id\":\"call_ws_tool_parity\"") != std::string::npos);
    REQUIRE(
        observed_plans[0].body.find("\"output\":[{\"type\":\"input_text\",\"text\":\"ok\"}]") != std::string::npos
    );
}

TEST_CASE(
    "responses websocket path retries without previous_response_id after previous_response_not_found",
    "[proxy][transport][ws][guard]"
) {
    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            if (plan.body.find("\"previous_response_id\":\"resp_stale_ws_guard\"") != std::string::npos) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 400,
                    .body =
                        R"({"error":{"type":"invalid_request_error","code":"previous_response_not_found","message":"Previous response not found.","param":"previous_response_id"}})",
                    .error_code = "previous_response_not_found",
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 101,
                .events = {
                    R"({"type":"response.created","response":{"id":"resp_guard_ws_recovered","status":"in_progress"}})",
                    R"({"type":"response.completed","response":{"id":"resp_guard_ws_recovered","object":"response","status":"completed","output":[]}})",
                },
                .accepted = true,
                .close_code = 1000,
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":"guard ws retry","previous_response_id":"resp_stale_ws_guard"})";
    const auto response = tightrope::server::controllers::proxy_responses_websocket("/v1/responses", payload);

    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(observed_plans.size() == 2);
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\":\"resp_stale_ws_guard\"") != std::string::npos);
    REQUIRE(observed_plans[1].body.find("\"previous_response_id\"") == std::string::npos);
}

TEST_CASE(
    "responses websocket path retries when upstream returns 101 with previous_response_not_found event",
    "[proxy][transport][ws][guard]"
) {
    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            if (plan.body.find("\"previous_response_id\":\"resp_stale_ws_guard_101\"") == std::string::npos) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 101,
                    .events = {
                        R"({"type":"response.created","response":{"id":"resp_guard_ws_recovered_101","status":"in_progress"}})",
                        R"({"type":"response.completed","response":{"id":"resp_guard_ws_recovered_101","object":"response","status":"completed","output":[]}})",
                    },
                    .accepted = true,
                    .close_code = 1000,
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 101,
                .events = {
                    R"({"type":"response.failed","response":{"id":"resp_guard_ws_failed","status":"failed"},"error":{"type":"invalid_request_error","code":"previous_response_not_found","message":"Previous response not found.","param":"previous_response_id"}})",
                    R"({"type":"response.completed","response":{"id":"resp_guard_ws_failed","object":"response","status":"failed","output":[]}})",
                },
                .accepted = true,
                .close_code = 1000,
                .error_code = "previous_response_not_found",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":"guard ws retry 101","previous_response_id":"resp_stale_ws_guard_101"})";
    const auto response = tightrope::server::controllers::proxy_responses_websocket("/v1/responses", payload);

    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(observed_plans.size() == 2);
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\":\"resp_stale_ws_guard_101\"") != std::string::npos);
    REQUIRE(observed_plans[1].body.find("\"previous_response_id\"") == std::string::npos);
    REQUIRE(response.frames[1].find("\"resp_guard_ws_recovered_101\"") != std::string::npos);
}

TEST_CASE(
    "responses websocket path does not strip previous_response_id for function_call_output payloads",
    "[proxy][transport][ws][guard]"
) {
    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            if (observed_plans.size() == 1) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 400,
                    .body =
                        R"({"error":{"type":"invalid_request_error","code":"previous_response_not_found","message":"Previous response not found.","param":"previous_response_id"}})",
                    .error_code = "previous_response_not_found",
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 101,
                .events = {
                    R"({"type":"response.created","response":{"id":"resp_guard_ws_tool_output_unexpected","status":"in_progress"}})",
                    R"({"type":"response.completed","response":{"id":"resp_guard_ws_tool_output_unexpected","object":"response","status":"completed","output":[]}})",
                },
                .accepted = true,
                .close_code = 1000,
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":[{"type":"function_call_output","call_id":"call_guard_ws_tool","output":"ok"}],"previous_response_id":"resp_stale_ws_guard_tool"})";
    const auto response = tightrope::server::controllers::proxy_responses_websocket("/v1/responses", payload);

    REQUIRE(response.status == 400);
    REQUIRE_FALSE(response.accepted);
    REQUIRE(observed_plans.size() == 1);
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\":\"resp_stale_ws_guard_tool\"") != std::string::npos);
}

TEST_CASE(
    "responses websocket path does not retry previous_response guard for function_call_output when upstream returns 101 error event",
    "[proxy][transport][ws][guard]"
) {
    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            if (observed_plans.size() == 1) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 101,
                    .events = {
                        R"({"type":"response.failed","response":{"id":"resp_guard_ws_tool_failed","status":"failed"},"error":{"type":"invalid_request_error","code":"previous_response_not_found","message":"Previous response not found.","param":"previous_response_id"}})",
                    },
                    .accepted = true,
                    .close_code = 1000,
                    .error_code = "previous_response_not_found",
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 101,
                .events = {
                    R"({"type":"response.created","response":{"id":"resp_guard_ws_tool_retry_unexpected","status":"in_progress"}})",
                    R"({"type":"response.completed","response":{"id":"resp_guard_ws_tool_retry_unexpected","object":"response","status":"completed","output":[]}})",
                },
                .accepted = true,
                .close_code = 1000,
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":[{"type":"function_call_output","call_id":"call_guard_ws_tool_101","output":"ok"}],"previous_response_id":"resp_stale_ws_guard_tool_101"})";
    const auto response = tightrope::server::controllers::proxy_responses_websocket("/v1/responses", payload);

    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(observed_plans.size() == 1);
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\":\"resp_stale_ws_guard_tool_101\"") != std::string::npos);
}

TEST_CASE(
    "backend websocket path preflights bridged previous_response_id for function_call_output payloads",
    "[proxy][transport][ws][guard][backend][bridge]"
) {
    const auto bridge_db_path = make_temp_proxy_db_path("bridge-ws-tool-previous-backend-rewrite");
    sqlite3* db = nullptr;
    REQUIRE(
        sqlite3_open_v2(bridge_db_path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) ==
        SQLITE_OK
    );
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::ensure_proxy_response_continuity_schema(db));
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()
    )
                            .count();
    REQUIRE(tightrope::db::upsert_proxy_response_continuity(
        db,
        "transport-http-bridge-ws-tool-backend",
        "",
        "resp_current_ws_tool_backend",
        "acc-ws-tool-backend",
        now_ms,
        120000
    ));
    sqlite3_close(db);

    EnvVarGuard db_path_guard("TIGHTROPE_DB_PATH");
    REQUIRE(setenv("TIGHTROPE_DB_PATH", bridge_db_path.string().c_str(), 1) == 0);

    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            const bool has_current = plan.body.find("\"previous_response_id\":\"resp_current_ws_tool_backend\"") !=
                                     std::string::npos;
            if (has_current) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 101,
                    .events = {
                        R"({"type":"response.created","response":{"id":"resp_guard_ws_tool_backend_retried","status":"in_progress"}})",
                        R"({"type":"response.completed","response":{"id":"resp_guard_ws_tool_backend_retried","object":"response","status":"completed","output":[]}})",
                    },
                    .accepted = true,
                    .close_code = 1000,
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 101,
                .events = {
                    R"({"type":"response.failed","response":{"id":"resp_guard_ws_tool_backend_failed","status":"failed"},"error":{"type":"invalid_request_error","code":"previous_response_not_found","message":"Previous response not found.","param":"previous_response_id"}})",
                },
                .accepted = true,
                .close_code = 1000,
                .error_code = "previous_response_not_found",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":[{"type":"function_call_output","call_id":"call_guard_ws_tool_backend","output":"ok"}],"previous_response_id":"resp_stale_ws_tool_backend_rewrite"})";
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"session_id", "transport-http-bridge-ws-tool-backend"},
    };
    const auto response =
        tightrope::server::controllers::proxy_responses_websocket("/backend-api/codex/responses", payload, inbound);

    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(observed_plans.size() == 1);
    REQUIRE(
        observed_plans[0].body.find("\"previous_response_id\":\"resp_current_ws_tool_backend\"") != std::string::npos
    );
    REQUIRE(observed_plans[0].body.find("\"resp_stale_ws_tool_backend_rewrite\"") == std::string::npos);
    REQUIRE(response.frames.size() == 2);
    REQUIRE(response.frames[1].find("\"resp_guard_ws_tool_backend_retried\"") != std::string::npos);

    std::filesystem::remove(bridge_db_path);
}

TEST_CASE(
    "responses websocket path preserves function_call_output previous_response_id on previous_response_not_found",
    "[proxy][transport][ws][guard][bridge]"
) {
    const auto bridge_db_path = make_temp_proxy_db_path("bridge-ws-tool-previous-rewrite");
    sqlite3* db = nullptr;
    REQUIRE(
        sqlite3_open_v2(bridge_db_path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) ==
        SQLITE_OK
    );
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::db::ensure_proxy_response_continuity_schema(db));
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()
    )
                            .count();
    REQUIRE(tightrope::db::upsert_proxy_response_continuity(
        db,
        "transport-http-bridge-ws-tool",
        "",
        "resp_current_ws_tool",
        "acc-ws-tool",
        now_ms,
        120000
    ));
    sqlite3_close(db);

    EnvVarGuard db_path_guard("TIGHTROPE_DB_PATH");
    REQUIRE(setenv("TIGHTROPE_DB_PATH", bridge_db_path.string().c_str(), 1) == 0);

    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 101,
                .events = {
                    R"({"type":"response.failed","response":{"id":"resp_ws_tool_failed","status":"failed"},"error":{"type":"invalid_request_error","code":"previous_response_not_found","message":"Previous response not found.","param":"previous_response_id"}})",
                },
                .accepted = true,
                .close_code = 1000,
                .error_code = "previous_response_not_found",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":[{"type":"function_call_output","call_id":"call_bridge_ws_tool","output":"ok"}],"previous_response_id":"resp_stale_ws_tool"})";
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"session_id", "transport-http-bridge-ws-tool"},
    };
    const auto response = tightrope::server::controllers::proxy_responses_websocket("/v1/responses", payload, inbound);

    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(observed_plans.size() == 1);
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\":\"resp_stale_ws_tool\"") != std::string::npos);

    std::filesystem::remove(bridge_db_path);
}

TEST_CASE(
    "backend websocket path strips unknown previous_response_id before upstream",
    "[proxy][transport][ws][guard]"
) {
    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 101,
                .events = {
                    R"({"type":"response.created","response":{"id":"resp_backend_guard_stripped","status":"in_progress"}})",
                    R"({"type":"response.completed","response":{"id":"resp_backend_guard_stripped","object":"response","status":"completed","output":[]}})",
                },
                .accepted = true,
                .close_code = 1000,
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":"backend guard","previous_response_id":"resp_backend_guard_prev"})";
    const auto response = tightrope::server::controllers::proxy_responses_websocket("/backend-api/codex/responses", payload);

    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(observed_plans.size() == 1);
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\"") == std::string::npos);
    REQUIRE(response.frames.size() == 2);
    REQUIRE(response.frames[1].find("\"resp_backend_guard_stripped\"") != std::string::npos);
}

TEST_CASE("backend websocket route marks upstream websocket session preservation", "[proxy][transport][ws][guard]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 101,
        .events = {
            R"({"type":"response.created","response":{"id":"resp_backend_preserve","status":"in_progress"}})",
            R"({"type":"response.completed","response":{"id":"resp_backend_preserve","object":"response","status":"completed","output":[]}})",
        },
        .accepted = true,
        .close_code = 1000,
    };
    tightrope::proxy::set_upstream_transport(fake);

    const std::string payload = R"({"model":"gpt-5.4","input":"backend preserve"})";
    const auto backend =
        tightrope::server::controllers::proxy_responses_websocket("/backend-api/codex/responses", payload);
    REQUIRE(backend.status == 101);
    REQUIRE(backend.accepted);
    REQUIRE(fake->last_plan.preserve_upstream_websocket_session);

    const auto openai = tightrope::server::controllers::proxy_responses_websocket("/v1/responses", payload);
    REQUIRE(openai.status == 101);
    REQUIRE(openai.accepted);
    REQUIRE_FALSE(fake->last_plan.preserve_upstream_websocket_session);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE(
    "responses websocket path does not perform synthetic pre-created retries",
    "[proxy][transport][ws][parity]"
) {
    std::vector<tightrope::proxy::openai::UpstreamRequestPlan> observed_plans;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&observed_plans](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            observed_plans.push_back(plan);
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 101,
                .events = {R"({"type":"response.output_text.delta","delta":"partial"})"},
                .accepted = true,
                .close_code = 1011,
                .error_code = "stream_incomplete",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto response =
        tightrope::server::controllers::proxy_responses_websocket("/v1/responses", fixture.request.body);

    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(response.close_code == 1011);
    REQUIRE(observed_plans.size() == 1);
    REQUIRE(response.frames.size() == 1);
    REQUIRE(response.frames.front().find("\"response.output_text.delta\"") != std::string::npos);
}

TEST_CASE("websocket proxy path rejects invalid payload before upstream call", "[proxy][transport][ws]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    tightrope::proxy::set_upstream_transport(fake);

    const auto response = tightrope::server::controllers::proxy_responses_websocket(
        "/v1/responses",
        R"({"input":"missing-model"})"
    );

    REQUIRE_FALSE(fake->called);
    REQUIRE(response.status == 400);
    REQUIRE_FALSE(response.accepted);
    REQUIRE(response.close_code == 1008);
    REQUIRE_FALSE(response.frames.empty());
    REQUIRE(response.frames.front().find("\"type\":\"error\"") != std::string::npos);
    REQUIRE(response.frames.front().find("\"status\":400") != std::string::npos);
    REQUIRE(response.frames.front().find("\"code\":\"invalid_request_error\"") != std::string::npos);
    REQUIRE(response.frames.front().find("\"type\":\"invalid_request_error\"") != std::string::npos);
    REQUIRE(response.frames.front().find("\"param\":\"input\"") != std::string::npos);
    REQUIRE(response.frames.front().find("\"message\":\"Invalid request payload\"") != std::string::npos);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("responses JSON path rejects invalid payload with parity error envelope", "[proxy][transport]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    tightrope::proxy::set_upstream_transport(fake);

    const auto response =
        tightrope::server::controllers::post_proxy_responses_json("/v1/responses", R"({"input":"missing-model"})");

    REQUIRE_FALSE(fake->called);
    REQUIRE(response.status == 400);
    REQUIRE(response.body.find("\"code\":\"invalid_request_error\"") != std::string::npos);
    REQUIRE(response.body.find("\"type\":\"invalid_request_error\"") != std::string::npos);
    REQUIRE(response.body.find("\"param\":\"input\"") != std::string::npos);
    REQUIRE(response.body.find("\"message\":\"Invalid request payload\"") != std::string::npos);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("responses SSE path rejects invalid payload with parity error envelope", "[proxy][transport]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    tightrope::proxy::set_upstream_transport(fake);

    const auto response =
        tightrope::server::controllers::post_proxy_responses_sse("/v1/responses", R"({"input":"missing-model"})");

    REQUIRE_FALSE(fake->called);
    REQUIRE(response.status == 400);
    REQUIRE(response.events.size() == 1);
    REQUIRE(response.events.front().find("\"type\":\"response.failed\"") != std::string::npos);
    REQUIRE(response.events.front().find("\"code\":\"invalid_request_error\"") != std::string::npos);
    REQUIRE(response.events.front().find("\"error\":{\"message\":\"Invalid request payload\"") != std::string::npos);
    REQUIRE(response.events.front().find("\"type\":\"invalid_request_error\"") != std::string::npos);
    REQUIRE(response.events.front().find("\"param\":\"input\"") != std::string::npos);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("websocket proxy path accepts response.create payload envelope", "[proxy][transport][ws]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 101,
        .body = "",
        .events = {
            R"({"type":"response.created","response":{"id":"resp_ws_wrapped","status":"in_progress"}})",
            R"({"type":"response.completed","response":{"id":"resp_ws_wrapped","object":"response","status":"completed","output":[]}})",
        },
        .accepted = true,
        .close_code = 1000,
    };
    tightrope::proxy::set_upstream_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto wrapped_body = std::string(R"({"type":"response.create","response":)") + fixture.request.body + "}";
    const auto response =
        tightrope::server::controllers::proxy_responses_websocket("/backend-api/codex/responses", wrapped_body);

    REQUIRE(fake->called);
    REQUIRE(fake->last_plan.body.find("\"type\":\"response.create\"") != std::string::npos);
    REQUIRE(fake->last_plan.body.find("\"response\":") == std::string::npos);
    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(response.close_code == 1000);
    REQUIRE(response.frames.size() == 2);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("websocket proxy path accepts flat response.create payload", "[proxy][transport][ws]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 101,
        .body = "",
        .events = {
            R"({"type":"response.created","response":{"id":"resp_ws_flat","status":"in_progress"}})",
            R"({"type":"response.completed","response":{"id":"resp_ws_flat","object":"response","status":"completed","output":[]}})",
        },
        .accepted = true,
        .close_code = 1000,
    };
    tightrope::proxy::set_upstream_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto flat_body = std::string(R"({"type":"response.create",)") + fixture.request.body.substr(1);
    const auto response =
        tightrope::server::controllers::proxy_responses_websocket("/backend-api/codex/responses", flat_body);

    REQUIRE(fake->called);
    REQUIRE(fake->last_plan.body.find("\"type\":\"response.create\"") != std::string::npos);
    REQUIRE(fake->last_plan.body.find("\"response\":") == std::string::npos);
    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(response.close_code == 1000);
    REQUIRE(response.frames.size() == 2);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE(
    "backend websocket path strips unknown top-level previous_response_id when response object is provided",
    "[proxy][transport][ws]"
) {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 101,
        .body = "",
        .events = {
            R"({"type":"response.created","response":{"id":"resp_ws_merged","status":"in_progress"}})",
            R"({"type":"response.completed","response":{"id":"resp_ws_merged","object":"response","status":"completed","output":[]}})",
        },
        .accepted = true,
        .close_code = 1000,
    };
    tightrope::proxy::set_upstream_transport(fake);

    const auto wrapped_body = std::string(
        R"({"type":"response.create","previous_response_id":"resp_outer_previous","response":{"model":"gpt-5.4","input":"merge test"}})"
    );
    const auto response =
        tightrope::server::controllers::proxy_responses_websocket("/backend-api/codex/responses", wrapped_body);

    REQUIRE(fake->called);
    REQUIRE(fake->last_plan.body.find("\"type\":\"response.create\"") != std::string::npos);
    REQUIRE(fake->last_plan.body.find("merge test") != std::string::npos);
    REQUIRE(fake->last_plan.body.find("\"previous_response_id\"") == std::string::npos);
    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(response.close_code == 1000);
    REQUIRE(response.frames.size() == 2);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("websocket proxy path accepts UTF-16LE response.create payload", "[proxy][transport][ws]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 101,
        .body = "",
        .events = {
            R"({"type":"response.created","response":{"id":"resp_ws_utf16","status":"in_progress"}})",
            R"({"type":"response.completed","response":{"id":"resp_ws_utf16","object":"response","status":"completed","output":[]}})",
        },
        .accepted = true,
        .close_code = 1000,
    };
    tightrope::proxy::set_upstream_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto flat_body = std::string(R"({"type":"response.create",)") + fixture.request.body.substr(1);
    const auto utf16_payload = encode_utf16le_with_bom(flat_body);
    const auto response =
        tightrope::server::controllers::proxy_responses_websocket("/backend-api/codex/responses", utf16_payload);

    REQUIRE(fake->called);
    REQUIRE(fake->last_plan.body.find("\"type\":\"response.create\"") != std::string::npos);
    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(response.close_code == 1000);
    REQUIRE(response.frames.size() == 2);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("websocket proxy path accepts framed response.create payload text", "[proxy][transport][ws]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 101,
        .body = "",
        .events = {
            R"({"type":"response.created","response":{"id":"resp_ws_framed","status":"in_progress"}})",
            R"({"type":"response.completed","response":{"id":"resp_ws_framed","object":"response","status":"completed","output":[]}})",
        },
        .accepted = true,
        .close_code = 1000,
    };
    tightrope::proxy::set_upstream_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto flat_body = std::string(R"({"type":"response.create",)") + fixture.request.body.substr(1);
    const auto framed_body = std::string("frame:") + flat_body + "\n";
    const auto response =
        tightrope::server::controllers::proxy_responses_websocket("/backend-api/codex/responses", framed_body);

    REQUIRE(fake->called);
    REQUIRE(fake->last_plan.body.find("\"type\":\"response.create\"") != std::string::npos);
    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(response.close_code == 1000);
    REQUIRE(response.frames.size() == 2);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("websocket proxy path sanitizes control-byte content in response.create payload", "[proxy][transport][ws]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 101,
        .body = "",
        .events = {
            R"({"type":"response.created","response":{"id":"resp_ws_ctrl","status":"in_progress"}})",
            R"({"type":"response.completed","response":{"id":"resp_ws_ctrl","object":"response","status":"completed","output":[]}})",
        },
        .accepted = true,
        .close_code = 1000,
    };
    tightrope::proxy::set_upstream_transport(fake);

    std::string payload = R"({"type":"response.create",)";
    payload.push_back('\0');
    payload.push_back(static_cast<char>(0x85));
    payload += R"("response":{"model":"gpt-5.4","input":"prefix)";
    payload.push_back('\0');
    payload.push_back(static_cast<char>(0x1B));
    payload.push_back(static_cast<char>(0x85));
    payload += R"([36m<body>suffix"}})";
    payload.push_back('\0');
    payload.push_back('\0');
    payload += "[39m";
    const auto response =
        tightrope::server::controllers::proxy_responses_websocket("/backend-api/codex/responses", payload);

    CAPTURE(fake->last_plan.body);
    REQUIRE(fake->called);
    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(response.close_code == 1000);
    REQUIRE(response.frames.size() == 2);
    REQUIRE(fake->last_plan.body.find("\\u0000") == std::string::npos);
    REQUIRE(fake->last_plan.body.find("\\u001B") == std::string::npos);
    REQUIRE(fake->last_plan.body.find("\\u001b") == std::string::npos);
    REQUIRE(fake->last_plan.body.find("\\u0085") == std::string::npos);
    REQUIRE(fake->last_plan.body.find("[36m<body>suffix") != std::string::npos);
    REQUIRE(fake->last_plan.body.find('\0') == std::string::npos);
    REQUIRE(fake->last_plan.body.find('\x1b') == std::string::npos);
    REQUIRE(fake->last_plan.body.find(static_cast<char>(0x85)) == std::string::npos);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("websocket proxy path passes through response.create payload when normalizer parse fails", "[proxy][transport][ws]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 101,
        .body = "",
        .events = {
            R"({"type":"response.created","response":{"id":"resp_ws_passthrough","status":"in_progress"}})",
            R"({"type":"response.completed","response":{"id":"resp_ws_passthrough","object":"response","status":"completed","output":[]}})",
        },
        .accepted = true,
        .close_code = 1000,
    };
    tightrope::proxy::set_upstream_transport(fake);

    const auto passthrough_body =
        R"({"type":"response.create","model":"gpt-5.4","input":[],"response":NaN})";
    const auto response =
        tightrope::server::controllers::proxy_responses_websocket("/backend-api/codex/responses", passthrough_body);

    REQUIRE(fake->called);
    REQUIRE(fake->last_plan.body == passthrough_body);
    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(response.close_code == 1000);
    REQUIRE(response.frames.size() == 2);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("websocket proxy path passes through non-response.create payloads", "[proxy][transport][ws]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 101,
        .body = "",
        .events = {
            R"({"type":"response.completed","response":{"id":"resp_ws_cancel","object":"response","status":"cancelled","output":[]}})",
        },
        .accepted = true,
        .close_code = 1000,
    };
    tightrope::proxy::set_upstream_transport(fake);

    const auto passthrough_body = R"({"type":"response.cancel","response_id":"resp_ws_cancel"})";
    const auto response =
        tightrope::server::controllers::proxy_responses_websocket("/backend-api/codex/responses", passthrough_body);

    REQUIRE(fake->called);
    REQUIRE(fake->last_plan.body == passthrough_body);
    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(response.close_code == 1000);
    REQUIRE(response.frames.size() == 1);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("websocket proxy path forwards binary frames without JSON normalization", "[proxy][transport][ws]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 101,
        .body = "",
        .events = {
            R"({"type":"response.completed","response":{"id":"resp_ws_binary","object":"response","status":"completed","output":[]}})",
        },
        .accepted = true,
        .close_code = 1000,
    };
    tightrope::proxy::set_upstream_transport(fake);

    const std::string binary_payload("\x01\x02\x03\x04\x00\xff", 6);
    const auto response =
        tightrope::server::controllers::proxy_responses_websocket(
            "/backend-api/codex/responses",
            binary_payload,
            {},
            true
        );

    REQUIRE(fake->called);
    REQUIRE(fake->last_plan.body == binary_payload);
    REQUIRE(fake->last_plan.websocket_binary_payload);
    REQUIRE(fake->last_plan.preserve_upstream_websocket_session);
    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(response.close_code == 1000);
    REQUIRE(response.frames.size() == 1);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("websocket proxy path maps upstream failures to failed close transcript", "[proxy][transport][ws]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 503,
        .body = "",
        .events = {},
        .accepted = false,
        .close_code = 1011,
        .error_code = "no_accounts",
    };
    tightrope::proxy::set_upstream_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto response =
        tightrope::server::controllers::proxy_responses_websocket("/backend-api/codex/responses", fixture.request.body);

    REQUIRE(fake->called);
    REQUIRE(response.status == 503);
    REQUIRE_FALSE(response.accepted);
    REQUIRE(response.close_code == 1011);
    REQUIRE_FALSE(response.frames.empty());
    REQUIRE(response.frames.front().find("\"type\":\"error\"") != std::string::npos);
    REQUIRE(response.frames.front().find("\"status\":503") != std::string::npos);
    REQUIRE(response.frames.front().find("\"code\":\"no_accounts\"") != std::string::npos);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE(
    "websocket proxy path closes incomplete accepted streams without synthetic fallback events",
    "[proxy][transport][ws][parity]"
) {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 101,
        .body = "",
        .events = {},
        .accepted = true,
        .close_code = 1011,
        .error_code = "",
    };
    tightrope::proxy::set_upstream_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto response =
        tightrope::server::controllers::proxy_responses_websocket("/v1/responses", fixture.request.body);

    REQUIRE(fake->called);
    REQUIRE(response.status == 101);
    REQUIRE_FALSE(response.accepted);
    REQUIRE(response.close_code == 1011);
    REQUIRE(response.frames.empty());

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("compact and transcribe paths return upstream payloads", "[proxy][transport]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    tightrope::proxy::set_upstream_transport(fake);

    fake->next_result = {
        .status = 200,
        .body = R"({"id":"resp_compact_upstream","status":"completed","output":[]})",
        .events = {},
    };
    const auto compact = tightrope::server::controllers::post_proxy_responses_compact(
        "/backend-api/codex/responses/compact",
        R"({"model":"gpt-5.4","input":"compact"})"
    );
    REQUIRE(fake->called);
    REQUIRE(fake->last_plan.path == "/codex/responses/compact");
    REQUIRE(compact.status == 200);
    REQUIRE(compact.body.find("\"resp_compact_upstream\"") != std::string::npos);

    fake->called = false;
    fake->next_result = {
        .status = 200,
        .body = R"({"text":"upstream transcript"})",
        .events = {},
    };
    const auto transcribe = tightrope::server::controllers::post_proxy_transcribe(
        "/backend-api/transcribe",
        "gpt-4o-transcribe",
        "hello",
        "audio-bytes"
    );
    REQUIRE(fake->called);
    REQUIRE(fake->last_plan.path == "/transcribe");
    REQUIRE(transcribe.status == 200);
    REQUIRE(transcribe.body.find("\"upstream transcript\"") != std::string::npos);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("upstream failures are surfaced with proxy error mapping", "[proxy][transport]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 503,
        .body = "",
        .events = {},
    };
    tightrope::proxy::set_upstream_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto json_response =
        tightrope::server::controllers::post_proxy_responses_json("/v1/responses", fixture.request.body);
    REQUIRE(json_response.status == 503);
    REQUIRE(json_response.body.find("\"error\"") != std::string::npos);
    REQUIRE(json_response.body.find("\"code\":\"upstream_error\"") != std::string::npos);
    REQUIRE(json_response.body.find("\"type\":\"server_error\"") != std::string::npos);

    const auto sse_response =
        tightrope::server::controllers::post_proxy_responses_sse("/v1/responses", fixture.request.body);
    REQUIRE(sse_response.status == 503);
    REQUIRE_FALSE(sse_response.events.empty());
    REQUIRE(sse_response.events.back().find("\"type\":\"response.failed\"") != std::string::npos);
    REQUIRE(sse_response.events.back().find("\"code\":\"upstream_error\"") != std::string::npos);
    REQUIRE(sse_response.events.back().find("\"type\":\"server_error\"") != std::string::npos);
    REQUIRE(sse_response.events.back().find("\"message\":\"Upstream error\"") != std::string::npos);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("proxy transport emits request lifecycle logs for JSON responses", "[proxy][transport][logging]") {
    std::vector<tightrope::core::logging::LogRecord> observed;
    tightrope::core::logging::set_log_observer_for_tests(
        [&observed](const tightrope::core::logging::LogRecord& record) { observed.push_back(record); });

    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 200,
        .body = R"({"id":"resp_log","object":"response","status":"completed","output":[]})",
        .events = {},
    };
    tightrope::proxy::set_upstream_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto response = tightrope::server::controllers::post_proxy_responses_json("/v1/responses", fixture.request.body);

    tightrope::proxy::reset_upstream_transport();
    tightrope::core::logging::clear_log_observer_for_tests();

    REQUIRE(response.status == 200);
    REQUIRE(has_event(observed, "proxy", "responses_json_request_received"));
    REQUIRE(has_event(observed, "proxy", "responses_json_upstream_plan_ready"));
    REQUIRE(has_event(observed, "proxy", "upstream_execute_start"));
    REQUIRE(has_event(observed, "proxy", "upstream_execute_complete"));
    REQUIRE(has_event(observed, "proxy", "responses_json_upstream_result"));
}

TEST_CASE("proxy transport does not emit upstream payload traces when disabled", "[proxy][transport][logging]") {
    EnvVarGuard trace_guard("TIGHTROPE_PROXY_TRACE_UPSTREAM_PAYLOAD");
    unsetenv("TIGHTROPE_PROXY_TRACE_UPSTREAM_PAYLOAD");

    std::vector<tightrope::core::logging::LogRecord> observed;
    tightrope::core::logging::set_log_observer_for_tests(
        [&observed](const tightrope::core::logging::LogRecord& record) { observed.push_back(record); });

    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 200,
        .body = R"({"id":"resp_log","object":"response","status":"completed","output":[]})",
        .events = {},
    };
    tightrope::proxy::set_upstream_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto response = tightrope::server::controllers::post_proxy_responses_json("/v1/responses", fixture.request.body);

    tightrope::proxy::reset_upstream_transport();
    tightrope::core::logging::clear_log_observer_for_tests();

    REQUIRE(response.status == 200);
    REQUIRE_FALSE(has_event(observed, "proxy", "upstream_payload_trace"));
}

TEST_CASE("proxy transport emits upstream payload trace when enabled", "[proxy][transport][logging]") {
    EnvVarGuard trace_guard("TIGHTROPE_PROXY_TRACE_UPSTREAM_PAYLOAD");
    setenv("TIGHTROPE_PROXY_TRACE_UPSTREAM_PAYLOAD", "1", 1);

    std::vector<tightrope::core::logging::LogRecord> observed;
    tightrope::core::logging::set_log_observer_for_tests(
        [&observed](const tightrope::core::logging::LogRecord& record) { observed.push_back(record); });

    auto fake = std::make_shared<FakeUpstreamTransport>();
    fake->next_result = {
        .status = 200,
        .body = R"({"id":"resp_log","object":"response","status":"completed","output":[]})",
        .events = {},
    };
    tightrope::proxy::set_upstream_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto response = tightrope::server::controllers::post_proxy_responses_json("/v1/responses", fixture.request.body);

    tightrope::proxy::reset_upstream_transport();
    tightrope::core::logging::clear_log_observer_for_tests();

    REQUIRE(response.status == 200);
    const auto trace = find_event(observed, "proxy", "upstream_payload_trace");
    REQUIRE(trace.has_value());
    REQUIRE(trace->detail.find("route=/v1/responses") != std::string::npos);
    REQUIRE(trace->detail.find("payload=") != std::string::npos);
}
