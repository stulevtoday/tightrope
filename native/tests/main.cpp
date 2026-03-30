#define CATCH_CONFIG_RUNNER
#include <catch2/catch_all.hpp>

#include <filesystem>
#include <optional>
#include <string_view>

namespace {

std::optional<std::filesystem::path> find_repo_root(std::filesystem::path start) {
    constexpr std::string_view kFixtureManifest = "native/tests/contracts/fixtures/http/manifest.json";
    while (true) {
        std::error_code ec;
        const auto has_manifest = std::filesystem::exists(start / kFixtureManifest, ec) && !ec;
        const auto has_cmake = std::filesystem::exists(start / "CMakeLists.txt", ec) && !ec;
        if (has_manifest && has_cmake) {
            return start;
        }

        const auto parent = start.parent_path();
        if (parent.empty() || parent == start) {
            return std::nullopt;
        }
        start = parent;
    }
}

void normalize_test_working_directory() {
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    if (ec) {
        return;
    }
    const auto repo_root = find_repo_root(cwd);
    if (!repo_root.has_value()) {
        return;
    }
    std::filesystem::current_path(*repo_root, ec);
}

} // namespace

int main(int argc, char* argv[]) {
    normalize_test_working_directory();
    Catch::Session session;
    return session.run(argc, argv);
}
