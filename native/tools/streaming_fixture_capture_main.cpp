#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "contracts/streaming_fixture_capture.h"

namespace {

std::filesystem::path default_reference_root() {
    if (const char* env = std::getenv("REFERENCE_REPO_ROOT"); env != nullptr && *env != '\0') {
        return env;
    }
    return "/Users/fabian/Development/codex-lb";
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path fixture_root = "native/tests/contracts/fixtures/streaming";
    std::filesystem::path reference_root = default_reference_root();

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--reference-root" && i + 1 < argc) {
            reference_root = argv[++i];
            continue;
        }
        if (arg == "--fixture-root" && i + 1 < argc) {
            fixture_root = argv[++i];
            continue;
        }
        std::cerr << "Unknown argument: " << arg << '\n';
        return 2;
    }

    try {
        const auto fixtures = tightrope::tests::contracts::capture_streaming_contract_fixtures(reference_root);
        tightrope::tests::contracts::write_captured_streaming_contract_fixtures(fixtures, fixture_root);
        std::cout << "Wrote streaming fixtures from " << reference_root << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Capture failed: " << ex.what() << '\n';
        return 1;
    }
}
