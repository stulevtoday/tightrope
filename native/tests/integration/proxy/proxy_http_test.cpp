#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include "controllers/proxy_controller.h"
#include "contracts/fixture_loader.h"
#include "tests/integration/proxy/include/test_support/fake_upstream_transport.h"

TEST_CASE("responses endpoint replays the frozen JSON contract", "[proxy][http]") {
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            if (plan.path == "/codex/responses") {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 200,
                    .body = R"({"id":"resp_from_upstream","object":"response","status":"completed","output":[]})",
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 404,
                .body = R"({"error":{"message":"not found","code":"not_found","type":"invalid_request_error"}})",
                .error_code = "not_found",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    REQUIRE_FALSE(fixture.request.body.empty());

    const auto response =
        tightrope::server::controllers::post_proxy_responses_json("/v1/responses", fixture.request.body);
    REQUIRE(response.status == fixture.response.status);
    REQUIRE(response.content_type == "application/json");
    REQUIRE(response.body.find("\"id\"") != std::string::npos);
    REQUIRE(response.body.find("\"object\"") != std::string::npos);
    REQUIRE(response.body.find("\"status\"") != std::string::npos);
    REQUIRE(response.body.find("\"output\"") != std::string::npos);
}

TEST_CASE("responses endpoint returns OpenAI error envelope for invalid payload", "[proxy][http]") {
    const std::string invalid_payload = R"({"input":"missing_model"})";
    const auto response = tightrope::server::controllers::post_proxy_responses_json("/v1/responses", invalid_payload);
    REQUIRE(response.status == 400);
    REQUIRE(response.content_type == "application/json");
    REQUIRE(response.body.find("\"error\"") != std::string::npos);
    REQUIRE(response.body.find("\"code\":\"invalid_request_error\"") != std::string::npos);
}

TEST_CASE("compact responses endpoint returns compact payload", "[proxy][http]") {
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            if (plan.path == "/codex/responses/compact") {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 200,
                    .body = R"({"object":"response.compact","id":"resp_compact_upstream","status":"completed","output":[]})",
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 404,
                .body = R"({"error":{"message":"not found","code":"not_found","type":"invalid_request_error"}})",
                .error_code = "not_found",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const std::string payload = R"({"model":"gpt-5.4","input":"compact hello"})";
    const auto v1_response =
        tightrope::server::controllers::post_proxy_responses_compact("/v1/responses/compact", payload);
    REQUIRE(v1_response.status == 200);
    REQUIRE(v1_response.content_type == "application/json");
    REQUIRE(v1_response.body.find("\"object\":\"response.compact\"") != std::string::npos);
    REQUIRE(v1_response.body.find("\"status\":\"completed\"") != std::string::npos);
    REQUIRE(v1_response.body.find("\"output\":[]") != std::string::npos);

    const auto backend_response =
        tightrope::server::controllers::post_proxy_responses_compact("/backend-api/codex/responses/compact", payload);
    REQUIRE(backend_response.status == 200);
    REQUIRE(backend_response.content_type == "application/json");
    REQUIRE(backend_response.body.find("\"object\":\"response.compact\"") != std::string::npos);
    REQUIRE(backend_response.body.find("\"status\":\"completed\"") != std::string::npos);
    REQUIRE(backend_response.body.find("\"output\":[]") != std::string::npos);
}

TEST_CASE("models endpoints return contract-compatible list shapes", "[proxy][http]") {
    const auto backend_models = tightrope::server::controllers::get_proxy_models("/api/models");
    REQUIRE(backend_models.status == 200);
    REQUIRE(backend_models.content_type == "application/json");
    REQUIRE(backend_models.body.find("\"models\"") != std::string::npos);

    const auto v1_models = tightrope::server::controllers::get_proxy_models("/v1/models");
    REQUIRE(v1_models.status == 200);
    REQUIRE(v1_models.content_type == "application/json");
    REQUIRE(v1_models.body.find("\"object\":\"list\"") != std::string::npos);
    REQUIRE(v1_models.body.find("\"data\"") != std::string::npos);

    const auto codex_models = tightrope::server::controllers::get_proxy_models("/backend-api/codex/models");
    REQUIRE(codex_models.status == 200);
    REQUIRE(codex_models.content_type == "application/json");
    REQUIRE(codex_models.body.find("\"object\":\"list\"") != std::string::npos);
    REQUIRE(codex_models.body.find("\"data\"") != std::string::npos);
    REQUIRE(codex_models.body == v1_models.body);
}

TEST_CASE("transcribe endpoints enforce model and return transcript", "[proxy][http]") {
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            if (plan.path == "/transcribe") {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 200,
                    .body = R"({"text":"transcribed"})",
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 404,
                .body = R"({"error":{"message":"not found","code":"not_found","type":"invalid_request_error"}})",
                .error_code = "not_found",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto backend = tightrope::server::controllers::post_proxy_transcribe(
        "/backend-api/transcribe", "gpt-4o-transcribe", "hello", "audio-bytes"
    );
    REQUIRE(backend.status == 200);
    REQUIRE(backend.body.find("\"text\"") != std::string::npos);

    const auto v1_invalid = tightrope::server::controllers::post_proxy_transcribe(
        "/v1/audio/transcriptions", "gpt-4o-mini", "hello", "audio-bytes"
    );
    REQUIRE(v1_invalid.status == 400);
    REQUIRE(v1_invalid.body.find("\"code\":\"invalid_request_error\"") != std::string::npos);
    REQUIRE(v1_invalid.body.find("\"type\":\"invalid_request_error\"") != std::string::npos);
    REQUIRE(v1_invalid.body.find("\"param\":\"model\"") != std::string::npos);

    const auto v1_valid = tightrope::server::controllers::post_proxy_transcribe(
        "/v1/audio/transcriptions", "gpt-4o-transcribe", "hello", "audio-bytes"
    );
    REQUIRE(v1_valid.status == 200);
    REQUIRE(v1_valid.body.find("\"text\"") != std::string::npos);
}
