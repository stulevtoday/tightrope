#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "stream/ws_handler.h"
#include "contracts/fixture_loader.h"

namespace {

std::vector<std::string> expected_v1_ws_frames(const std::vector<std::string>& lines) {
    std::vector<std::string> filtered;
    for (const auto& line : lines) {
        if (line.find("\"route\":\"/v1/responses\"") != std::string::npos) {
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

TEST_CASE("websocket proxy preserves turn state semantics", "[proxy][ws]") {
    const auto transcript = tightrope::proxy::stream::replay_ws_contract("/v1/responses");
    REQUIRE(transcript.accepted);
    REQUIRE(transcript.close_code == 1000);

    const auto golden = tightrope::tests::contracts::load_ndjson_fixture(
        "native/tests/contracts/fixtures/streaming/responses_ws.ndjson"
    );
    REQUIRE(event_types(transcript.frames) == event_types(expected_v1_ws_frames(golden)));
    REQUIRE(transcript.frames.front().find("\"type\":\"response.created\"") != std::string::npos);
    REQUIRE(transcript.frames.front().find("\"event_type\"") == std::string::npos);
    REQUIRE(transcript.frames.front().find("\"contract\":\"proxy-streaming-v1\"") == std::string::npos);
}
