#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> load_ndjson_lines(const std::filesystem::path& path) {
    std::vector<std::string> lines;
    std::ifstream input(path);
    if (!input.is_open()) {
        return lines;
    }

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

bool any_line_has(const std::vector<std::string>& lines, const std::string& token) {
    for (const std::string& line : lines) {
        if (line.find(token) != std::string::npos) {
            return true;
        }
    }
    return false;
}

int first_index_with(const std::vector<std::string>& lines, const std::string& token) {
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find(token) != std::string::npos) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int first_index_with_all(
    const std::vector<std::string>& lines,
    const std::string& first,
    const std::string& second,
    const std::string& third = ""
) {
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find(first) == std::string::npos) {
            continue;
        }
        if (lines[i].find(second) == std::string::npos) {
            continue;
        }
        if (!third.empty() && lines[i].find(third) == std::string::npos) {
            continue;
        }
        return static_cast<int>(i);
    }
    return -1;
}

} // namespace

TEST_CASE("streaming fixtures preserve ordered event transcripts", "[contracts][streaming]") {
    const auto sse_events = load_ndjson_lines("native/tests/contracts/fixtures/streaming/responses_sse.ndjson");
    REQUIRE(sse_events.size() > 3);
    REQUIRE(any_line_has(sse_events, "\"route\":\"/backend-api/codex/responses\""));
    REQUIRE(any_line_has(sse_events, "\"route\":\"/v1/responses\""));

    const int created_idx = first_index_with_all(
        sse_events,
        "\"route\":\"/backend-api/codex/responses\"",
        "\"event_type\":\"response.created\""
    );
    const int completed_idx = first_index_with_all(
        sse_events,
        "\"route\":\"/backend-api/codex/responses\"",
        "\"event_type\":\"response.completed\""
    );
    REQUIRE(created_idx >= 0);
    REQUIRE(completed_idx >= 0);
    REQUIRE(created_idx < completed_idx);

    const auto ws_events = load_ndjson_lines("native/tests/contracts/fixtures/streaming/responses_ws.ndjson");
    REQUIRE(ws_events.size() > 3);
    REQUIRE(any_line_has(ws_events, "\"route\":\"/backend-api/codex/responses\""));
    REQUIRE(any_line_has(ws_events, "\"route\":\"/v1/responses\""));
    REQUIRE(first_index_with(ws_events, "\"close_code\":1011") >= 0);
    REQUIRE(first_index_with(ws_events, "\"error_code\":\"stream_incomplete\"") >= 0);
}
