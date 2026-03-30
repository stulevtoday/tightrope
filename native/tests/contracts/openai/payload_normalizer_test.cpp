#include <catch2/catch_test_macros.hpp>

#include <string>

#include "contracts/fixture_loader.h"
#include "openai/error_envelope.h"
#include "openai/model_registry.h"
#include "openai/payload_normalizer.h"

TEST_CASE("responses payload normalizer preserves contract fields", "[proxy][normalize]") {
    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    REQUIRE_FALSE(fixture.request.body.empty());

    const auto normalized = tightrope::proxy::openai::normalize_request(fixture.request.body);
    REQUIRE(normalized.body.find("\"model\":\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"input\":[{") != std::string::npos);
    REQUIRE(normalized.body.find("\"type\":\"input_text\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"tools\":[]") != std::string::npos);
    REQUIRE(normalized.body.find("\"include\":[]") != std::string::npos);
    REQUIRE(normalized.body.find("\"prompt_cache_key\":\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"service_tier\":\"priority\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"reasoning\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"effort\":\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"summary\":\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"text\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"verbosity\":\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"store\":false") != std::string::npos);
    REQUIRE(normalized.body.find("\"instructions\":\"\"") != std::string::npos);
    REQUIRE(normalized.body.find("temperature") == std::string::npos);
    REQUIRE(normalized.body.find("prompt_cache_retention") == std::string::npos);
    REQUIRE(normalized.body.find("max_output_tokens") == std::string::npos);
    REQUIRE(normalized.body.find("safety_identifier") == std::string::npos);
    REQUIRE(normalized.body.find("promptCacheKey") == std::string::npos);
    REQUIRE(normalized.body.find("reasoningEffort") == std::string::npos);
    REQUIRE(normalized.body.find("textVerbosity") == std::string::npos);

    const auto registry = tightrope::proxy::openai::build_default_model_registry();
    REQUIRE(registry.list_models().empty());

    const auto error =
        tightrope::proxy::openai::build_error_envelope("invalid_request_error", "Invalid request payload");
    REQUIRE(error.find("\"code\":\"invalid_request_error\"") != std::string::npos);
}

TEST_CASE("responses payload normalizer preserves explicit values over aliases", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":"ping",
      "prompt_cache_key":"keep_me",
      "promptCacheKey":"thread_alias",
      "reasoning":{"effort":"low"},
      "reasoningEffort":"high",
      "text":{"verbosity":"low"},
      "textVerbosity":"high",
      "verbosity":"medium",
      "service_tier":" FAST "
    })";

    const auto normalized = tightrope::proxy::openai::normalize_request(raw_request);
    REQUIRE(normalized.body.find("\"prompt_cache_key\":\"keep_me\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"service_tier\":\"priority\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"effort\":\"low\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"effort\":\"high\"") == std::string::npos);
    REQUIRE(normalized.body.find("\"verbosity\":\"low\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"verbosity\":\"medium\"") == std::string::npos);
    REQUIRE(normalized.body.find("promptCacheKey") == std::string::npos);
    REQUIRE(normalized.body.find("reasoningEffort") == std::string::npos);
    REQUIRE(normalized.body.find("textVerbosity") == std::string::npos);
    REQUIRE(normalized.body.find("\"input\":[{") != std::string::npos);
}

TEST_CASE("compact payload normalizer strips store and unsupported advisory fields", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":"ping",
      "store": false,
      "promptCacheKey":"thread_alias",
      "promptCacheRetention":"session",
      "service_tier":"FAST",
      "temperature":0.1,
      "max_output_tokens":64,
      "safety_identifier":"sid",
      "reasoningEffort":"medium"
    })";

    const auto normalized = tightrope::proxy::openai::normalize_compact_request(raw_request);
    REQUIRE(normalized.body.find("\"model\":\"gpt-5.4\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"input\":[{") != std::string::npos);
    REQUIRE(normalized.body.find("\"prompt_cache_key\":\"thread_alias\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"service_tier\":\"priority\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"store\"") == std::string::npos);
    REQUIRE(normalized.body.find("\"prompt_cache_retention\"") == std::string::npos);
    REQUIRE(normalized.body.find("\"temperature\"") == std::string::npos);
    REQUIRE(normalized.body.find("\"max_output_tokens\"") == std::string::npos);
    REQUIRE(normalized.body.find("\"safety_identifier\"") == std::string::npos);
}

TEST_CASE("responses payload normalizer rejects unsupported include values", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":"ping",
      "include":["reasoning.encrypted_content","bad.include.value"]
    })";

    REQUIRE_THROWS(tightrope::proxy::openai::normalize_request(raw_request));
}

TEST_CASE("responses payload normalizer accepts known include values", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":"ping",
      "include":["reasoning.encrypted_content","web_search_call.action.sources"]
    })";

    const auto normalized = tightrope::proxy::openai::normalize_request(raw_request);
    REQUIRE(normalized.body.find("\"include\":[\"reasoning.encrypted_content\",\"web_search_call.action.sources\"]") !=
            std::string::npos);
}

TEST_CASE("responses payload normalizer rejects unsupported tool types", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":"ping",
      "tools":[{"type":"image_generation"}]
    })";

    REQUIRE_THROWS(tightrope::proxy::openai::normalize_request(raw_request));
}

TEST_CASE("responses payload normalizer normalizes web_search_preview aliases", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":"ping",
      "tools":[{"type":"web_search_preview"}],
      "tool_choice":{"type":"web_search_preview"}
    })";

    const auto normalized = tightrope::proxy::openai::normalize_request(raw_request);
    REQUIRE(normalized.body.find("\"tools\":[{\"type\":\"web_search\"}]") != std::string::npos);
    REQUIRE(normalized.body.find("\"tool_choice\":{\"type\":\"web_search\"}") != std::string::npos);
}

TEST_CASE("responses payload normalizer rejects truncation", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":"ping",
      "truncation":"auto"
    })";

    REQUIRE_THROWS(tightrope::proxy::openai::normalize_request(raw_request));
}

TEST_CASE("responses payload normalizer rejects input_file.file_id", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":[{"role":"user","content":[{"type":"input_file","file_id":"file_123"}]}]
    })";

    REQUIRE_THROWS(tightrope::proxy::openai::normalize_request(raw_request));
}

TEST_CASE("responses payload normalizer normalizes assistant and tool input roles", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":[
        {"role":"assistant","content":[{"type":"input_text","text":"Prior answer"}]},
        {"role":"tool","tool_call_id":"call_1","content":[{"type":"input_text","text":"{\"ok\":true}"}]},
        {"role":"user","content":[{"type":"input_text","text":"Continue"}]}
      ]
    })";

    const auto normalized = tightrope::proxy::openai::normalize_request(raw_request);
    REQUIRE(normalized.body.find("\"role\":\"assistant\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"type\":\"output_text\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"type\":\"function_call_output\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"call_id\":\"call_1\"") != std::string::npos);
    REQUIRE(normalized.body.find("\"output\":\"{\\\"ok\\\":true}\"") != std::string::npos);
}

TEST_CASE("responses payload normalizer rejects tool role input item without call id", "[proxy][normalize]") {
    const std::string raw_request = R"({
      "model":"gpt-5.4",
      "input":[{"role":"tool","content":[{"type":"input_text","text":"{\"ok\":true}"}]}]
    })";

    REQUIRE_THROWS(tightrope::proxy::openai::normalize_request(raw_request));
}
