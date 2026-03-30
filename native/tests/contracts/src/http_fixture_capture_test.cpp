#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "contract_io.h"
#include "http_fixture_capture.h"

namespace {

std::filesystem::path reference_repo_root() {
    if (const char* env = std::getenv("REFERENCE_REPO_ROOT"); env != nullptr && *env != '\0') {
        return env;
    }
    return "/Users/fabian/Development/codex-lb";
}

std::filesystem::path make_temp_fixture_root() {
    const auto root = std::filesystem::temp_directory_path() / "tightrope-contract-capture-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

} // namespace

TEST_CASE("http contract capture derives fixture metadata from codex-lb source", "[contracts][capture]") {
    const auto temp_root = make_temp_fixture_root();
    const std::filesystem::path seed_manifest = "native/tests/contracts/fixtures/http/manifest.json";
    std::filesystem::copy_file(seed_manifest, temp_root / "manifest.json", std::filesystem::copy_options::overwrite_existing);

    const auto fixtures =
        tightrope::tests::contracts::capture_http_contract_fixtures(reference_repo_root(), temp_root / "manifest.json");
    REQUIRE_FALSE(fixtures.empty());

    const auto it = std::find_if(fixtures.begin(), fixtures.end(), [](const auto& fixture) {
        return fixture.fixture_id == "responses_post";
    });
    REQUIRE(it != fixtures.end());
    REQUIRE_FALSE(it->request_body.empty());
    REQUIRE(it->request_body.find("\"promptCacheKey\"") != std::string::npos);

    tightrope::tests::contracts::write_captured_http_contract_fixtures(fixtures, temp_root, reference_repo_root());

    const auto manifest = tightrope::tests::contracts::read_text_file(temp_root / "manifest.json");
    REQUIRE(manifest.find("\"capture_mode\": \"source-derived-from-codex-lb\"") != std::string::npos);
}
