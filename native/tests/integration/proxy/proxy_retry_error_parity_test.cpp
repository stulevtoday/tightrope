#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "controllers/proxy_controller.h"
#include "contracts/fixture_loader.h"
#include "tests/integration/proxy/include/test_support/fake_upstream_transport.h"

TEST_CASE("responses SSE retries transient server_error once and succeeds", "[proxy][retry][stream]") {
    std::size_t call_count = 0;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&call_count](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            ++call_count;
            if (call_count == 1) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 500,
                    .body = "",
                    .events = {},
                    .accepted = false,
                    .close_code = 1000,
                    .error_code = "server_error",
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = "",
                .events = {
                    R"({"type":"response.created","response":{"id":"resp_retry_stream","status":"in_progress"}})",
                    R"({"type":"response.completed","response":{"id":"resp_retry_stream","object":"response","status":"completed","output":[]}})",
                },
                .accepted = false,
                .close_code = 1000,
                .error_code = "",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto response =
        tightrope::server::controllers::post_proxy_responses_sse("/v1/responses", fixture.request.body);

    REQUIRE(call_count == 2);
    REQUIRE(response.status == 200);
    REQUIRE(response.events.size() == 2);
    REQUIRE(response.events.back().find("\"type\":\"response.completed\"") != std::string::npos);
}

TEST_CASE("compact retries one same-contract attempt and returns success", "[proxy][retry][compact]") {
    std::size_t call_count = 0;
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [&call_count](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            ++call_count;
            if (call_count == 1) {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 502,
                    .body = "",
                    .events = {},
                    .accepted = false,
                    .close_code = 1000,
                    .error_code = "upstream_unavailable",
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 200,
                .body = R"({"object":"response.compaction","output":[]})",
                .events = {},
                .accepted = false,
                .close_code = 1000,
                .error_code = "",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto response = tightrope::server::controllers::post_proxy_responses_compact(
        "/v1/responses/compact",
        R"({"model":"gpt-5.4","input":"compact retry"})"
    );

    REQUIRE(call_count == 2);
    REQUIRE(response.status == 200);
    REQUIRE(response.body.find("\"object\":\"response.compaction\"") != std::string::npos);
}

TEST_CASE("responses JSON maps upstream transport failure to upstream_unavailable", "[proxy][errors][parity]") {
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 502,
                .body = "",
                .events = {},
                .accepted = false,
                .close_code = 1000,
                .error_code = "upstream_transport_failed",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto response =
        tightrope::server::controllers::post_proxy_responses_json("/v1/responses", fixture.request.body);

    REQUIRE(response.status == 502);
    REQUIRE(response.body.find("\"code\":\"upstream_unavailable\"") != std::string::npos);
}

TEST_CASE("responses websocket maps transport failures to stable upstream_unavailable code", "[proxy][errors][parity][ws]") {
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 503,
                .body = "",
                .events = {},
                .accepted = false,
                .close_code = 1011,
                .error_code = "upstream_transport_init_failed",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto response =
        tightrope::server::controllers::proxy_responses_websocket("/v1/responses", fixture.request.body);

    REQUIRE(response.status == 503);
    REQUIRE_FALSE(response.frames.empty());
    REQUIRE(response.frames.front().find("\"code\":\"upstream_unavailable\"") != std::string::npos);
}

TEST_CASE("compact preserves upstream_request_timeout code with stable message", "[proxy][errors][parity][compact]") {
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan&) {
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 502,
                .body = "",
                .events = {},
                .accepted = false,
                .close_code = 1000,
                .error_code = "upstream_request_timeout",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto response = tightrope::server::controllers::post_proxy_responses_compact(
        "/backend-api/codex/responses/compact",
        R"({"model":"gpt-5.4","input":"timeout"})"
    );

    REQUIRE(response.status == 502);
    REQUIRE(response.body.find("\"code\":\"upstream_request_timeout\"") != std::string::npos);
    REQUIRE(response.body.find("\"message\":\"Proxy request budget exhausted\"") != std::string::npos);
}
