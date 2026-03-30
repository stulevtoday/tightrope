#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "controllers/proxy_controller.h"
#include "contracts/fixture_loader.h"
#include "tests/integration/proxy/include/test_support/fake_upstream_transport.h"

namespace {

std::vector<std::string> filter_by_route(const std::vector<std::string>& lines, const std::string& route) {
    std::vector<std::string> filtered;
    for (const auto& line : lines) {
        if (line.find("\"route\":\"" + route + "\"") != std::string::npos &&
            line.find("\"event_type\"") != std::string::npos) {
            filtered.push_back(line);
        }
    }
    return filtered;
}

std::vector<std::string> event_types(const std::vector<std::string>& lines) {
    constexpr std::string_view event_type_marker = "\"event_type\":\"";
    constexpr std::string_view type_marker = "\"type\":\"";
    std::vector<std::string> types;
    for (const auto& line : lines) {
        auto start = line.find(event_type_marker);
        std::string_view marker = event_type_marker;
        if (start == std::string::npos) {
            start = line.find(type_marker);
            marker = type_marker;
        }
        if (start == std::string::npos) {
            continue;
        }
        const auto from = start + marker.size();
        const auto end = line.find('"', from);
        if (end == std::string::npos) {
            continue;
        }
        types.push_back(line.substr(from, end - from));
    }
    return types;
}

} // namespace

TEST_CASE("sse handler matches the golden transcript", "[proxy][sse]") {
    auto fake = std::make_shared<tightrope::tests::proxy::FakeUpstreamTransport>(
        [](const tightrope::proxy::openai::UpstreamRequestPlan& plan) {
            if (plan.path == "/codex/responses") {
                return tightrope::proxy::UpstreamExecutionResult{
                    .status = 200,
                    .events = {
                        R"({"type":"response.created","response":{"id":"resp_v1_success","status":"in_progress"}})",
                        R"({"type":"response.output_text.delta","delta":"ok"})",
                        R"({"type":"response.completed","response":{"id":"resp_v1_success","object":"response","status":"completed","output":[]}})",
                    },
                };
            }
            return tightrope::proxy::UpstreamExecutionResult{
                .status = 404,
                .error_code = "not_found",
            };
        }
    );
    const tightrope::tests::proxy::ScopedUpstreamTransport scoped_transport(fake);

    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto response =
        tightrope::server::controllers::post_proxy_responses_sse("/v1/responses", fixture.request.body);

    const auto golden = tightrope::tests::contracts::load_ndjson_fixture(
        "native/tests/contracts/fixtures/streaming/responses_sse.ndjson"
    );
    const auto expected = filter_by_route(golden, "/v1/responses");

    REQUIRE(response.status == 200);
    REQUIRE(response.content_type == "text/event-stream");
    REQUIRE(event_types(response.events) == event_types(expected));
    REQUIRE(response.events.front().find("\"type\":\"response.created\"") != std::string::npos);
    REQUIRE(response.events.front().find("\"event_type\"") == std::string::npos);
    REQUIRE(response.events.front().find("\"contract\":\"proxy-streaming-v1\"") == std::string::npos);
}
