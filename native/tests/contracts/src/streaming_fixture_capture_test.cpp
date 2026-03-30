#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "contract_io.h"
#include "streaming_fixture_capture.h"

namespace {

std::filesystem::path reference_repo_root() {
    if (const char* env = std::getenv("REFERENCE_REPO_ROOT"); env != nullptr && *env != '\0') {
        return env;
    }
    return "/Users/fabian/Development/codex-lb";
}

std::filesystem::path make_temp_fixture_root() {
    const auto root = std::filesystem::temp_directory_path() / "tightrope-streaming-contract-capture-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

bool any_line_has(const std::vector<std::string>& lines, const std::string& token) {
    for (const auto& line : lines) {
        if (line.find(token) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("streaming contract capture derives fixtures from codex-lb source contracts", "[contracts][capture]") {
    const auto fixtures = tightrope::tests::contracts::capture_streaming_contract_fixtures(reference_repo_root());
    REQUIRE(fixtures.sse_lines.size() >= 8);
    REQUIRE(fixtures.ws_lines.size() >= 7);

    REQUIRE(any_line_has(fixtures.sse_lines, "\"route\":\"/backend-api/codex/responses\""));
    REQUIRE(any_line_has(fixtures.sse_lines, "\"error_code\":\"stream_idle_timeout\""));
    REQUIRE(any_line_has(fixtures.ws_lines, "\"error_code\":\"stream_incomplete\""));
    REQUIRE(any_line_has(fixtures.ws_lines, "\"close_code\":1011"));

    const auto fixture_root = make_temp_fixture_root();
    tightrope::tests::contracts::write_captured_streaming_contract_fixtures(fixtures, fixture_root);

    const auto sse_file = tightrope::tests::contracts::read_text_file(fixture_root / "responses_sse.ndjson");
    const auto ws_file = tightrope::tests::contracts::read_text_file(fixture_root / "responses_ws.ndjson");
    REQUIRE(sse_file.find("\"event_type\":\"response.completed\"") != std::string::npos);
    REQUIRE(ws_file.find("\"maps_to_event_type\":\"response.failed\"") != std::string::npos);
}
