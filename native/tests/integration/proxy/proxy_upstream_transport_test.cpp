#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "logging/logger.h"
#include "openai/upstream_headers.h"
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
    "responses JSON path carries previous_response_id across requests with shared session continuity",
    "[proxy][transport][bridge]"
) {
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
    REQUIRE(observed_plans[1].body.find("\"previous_response_id\":\"resp_bridge_json_1\"") != std::string::npos);
}

TEST_CASE("responses JSON path reuses sticky account by prompt cache key", "[proxy][transport][sticky]") {
    const auto sticky_db_file =
        std::filesystem::temp_directory_path() / std::filesystem::path("tightrope-proxy-sticky-transport.sqlite3");
    std::filesystem::remove(sticky_db_file);
    EnvVarGuard db_path_guard("TIGHTROPE_DB_PATH");
    REQUIRE(setenv("TIGHTROPE_DB_PATH", sticky_db_file.string().c_str(), 1) == 0);

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

    tightrope::proxy::reset_upstream_transport();
    std::filesystem::remove(sticky_db_file);
}

TEST_CASE(
    "responses JSON path retries without previous_response_id after previous_response_not_found",
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
                .body = R"({"id":"resp_guard_json_success","object":"response","status":"completed","output":[]})",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":"guard json retry","previous_response_id":"resp_stale_json_guard"})";
    const auto response = tightrope::server::controllers::post_proxy_responses_json("/v1/responses", payload);

    REQUIRE(response.status == 200);
    REQUIRE(response.body.find("\"id\":\"resp_guard_json_success\"") != std::string::npos);
    REQUIRE(observed_plans.size() == 2);
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\":\"resp_stale_json_guard\"") != std::string::npos);
    REQUIRE(observed_plans[1].body.find("\"previous_response_id\"") == std::string::npos);
}

TEST_CASE(
    "responses SSE path carries previous_response_id across requests with shared session continuity",
    "[proxy][transport][bridge]"
) {
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
    REQUIRE(observed_plans[1].body.find("\"previous_response_id\":\"resp_bridge_sse_1\"") != std::string::npos);
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

TEST_CASE(
    "responses SSE path retries without previous_response_id after previous_response_not_found",
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
                    R"({"type":"response.created","response":{"id":"resp_guard_sse_success","status":"in_progress"}})",
                    R"({"type":"response.completed","response":{"id":"resp_guard_sse_success","object":"response","status":"completed","output":[]}})",
                },
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload =
        R"({"model":"gpt-5.4","input":"guard sse retry","previous_response_id":"resp_stale_sse_guard"})";
    const auto response = tightrope::server::controllers::post_proxy_responses_sse("/v1/responses", payload);

    REQUIRE(response.status == 200);
    REQUIRE(response.events.size() == 2);
    REQUIRE(observed_plans.size() == 2);
    REQUIRE(observed_plans[0].body.find("\"previous_response_id\":\"resp_stale_sse_guard\"") != std::string::npos);
    REQUIRE(observed_plans[1].body.find("\"previous_response_id\"") == std::string::npos);
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

TEST_CASE("websocket proxy path executes websocket request plan and normalizes events", "[proxy][transport][ws]") {
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
    REQUIRE(response.frames.front().find("response.output_text.delta") != std::string::npos);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE(
    "responses websocket path carries previous_response_id across requests with shared session continuity",
    "[proxy][transport][ws][bridge]"
) {
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
    REQUIRE(observed_plans[1].body.find("\"previous_response_id\":\"resp_bridge_ws_1\"") != std::string::npos);
}

TEST_CASE(
    "responses websocket path retries without previous_response_id after previous_response_not_found",
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
                    R"({"type":"response.created","response":{"id":"resp_guard_ws_success","status":"in_progress"}})",
                    R"({"type":"response.completed","response":{"id":"resp_guard_ws_success","object":"response","status":"completed","output":[]}})",
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
    "responses websocket path retries without previous_response_id when upstream returns 101 with previous_response_not_found event",
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
                        R"({"type":"response.failed","response":{"id":"resp_guard_ws_failed","status":"failed"},"error":{"type":"invalid_request_error","code":"previous_response_not_found","message":"Previous response not found.","param":"previous_response_id"}})",
                        R"({"type":"response.completed","response":{"id":"resp_guard_ws_failed","object":"response","status":"failed","output":[]}})",
                    },
                    .accepted = true,
                    .close_code = 1000,
                    .error_code = "previous_response_not_found",
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 101,
                .events = {
                    R"({"type":"response.created","response":{"id":"resp_guard_ws_success_101","status":"in_progress"}})",
                    R"({"type":"response.completed","response":{"id":"resp_guard_ws_success_101","object":"response","status":"completed","output":[]}})",
                },
                .accepted = true,
                .close_code = 1000,
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
    REQUIRE(response.frames.front().find("\"message\":\"Invalid request payload\"") != std::string::npos);

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

TEST_CASE("websocket proxy path maps empty accepted stream to stream_incomplete", "[proxy][transport][ws]") {
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
    REQUIRE(response.accepted);
    REQUIRE(response.close_code == 1011);
    REQUIRE(response.frames.size() == 1);
    REQUIRE(response.frames.front().find("\"type\":\"response.failed\"") != std::string::npos);
    REQUIRE(response.frames.front().find("\"code\":\"stream_incomplete\"") != std::string::npos);
    REQUIRE(response.frames.front().find("close_code=1011") != std::string::npos);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE("compact and transcribe paths return upstream payloads", "[proxy][transport]") {
    auto fake = std::make_shared<FakeUpstreamTransport>();
    tightrope::proxy::set_upstream_transport(fake);

    fake->next_result = {
        .status = 200,
        .body = R"({"object":"response.compact","id":"resp_compact_upstream","status":"completed"})",
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

    const auto sse_response =
        tightrope::server::controllers::post_proxy_responses_sse("/v1/responses", fixture.request.body);
    REQUIRE(sse_response.status == 503);
    REQUIRE_FALSE(sse_response.events.empty());
    REQUIRE(sse_response.events.back().find("\"type\":\"response.failed\"") != std::string::npos);
    REQUIRE(sse_response.events.back().find("\"code\":\"upstream_error\"") != std::string::npos);
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
