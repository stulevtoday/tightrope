#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "fixture_loader.h"
#include "source_contract_catalog.h"

namespace {

std::filesystem::path reference_repo_root() {
    if (const char* env = std::getenv("REFERENCE_REPO_ROOT"); env != nullptr && *env != '\0') {
        return env;
    }
    return "/Users/fabian/Development/codex-lb";
}

} // namespace

TEST_CASE("http fixture manifest entries map to codex-lb source routes", "[contracts][http]") {
    const auto manifest_entries = tightrope::tests::contracts::load_http_fixture_manifest();
    REQUIRE_FALSE(manifest_entries.empty());

    const auto source_routes = tightrope::tests::contracts::load_source_route_contracts(reference_repo_root());
    REQUIRE_FALSE(source_routes.empty());

    for (const auto& entry : manifest_entries) {
        const auto source = tightrope::tests::contracts::find_source_route_contract(source_routes, entry.method, entry.path);
        REQUIRE(source.has_value());
        REQUIRE(source->auth_mode == entry.auth_mode);
    }
}
