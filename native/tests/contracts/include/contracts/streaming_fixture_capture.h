#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace tightrope::tests::contracts {

struct CapturedStreamingFixtures {
    std::vector<std::string> sse_lines;
    std::vector<std::string> ws_lines;
};

CapturedStreamingFixtures capture_streaming_contract_fixtures(const std::filesystem::path& reference_repo_root);

void write_captured_streaming_contract_fixtures(
    const CapturedStreamingFixtures& fixtures,
    const std::filesystem::path& fixture_root
);

} // namespace tightrope::tests::contracts
