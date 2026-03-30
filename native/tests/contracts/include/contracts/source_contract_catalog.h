#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tightrope::tests::contracts {

struct SourceRouteContract {
    std::string method;
    std::string path;
    std::string auth_mode;
    std::string response_model;
    std::vector<std::string> response_body_keys;
    std::filesystem::path source_file;
};

std::vector<SourceRouteContract> load_source_route_contracts(const std::filesystem::path& reference_repo_root);

std::optional<SourceRouteContract> find_source_route_contract(
    const std::vector<SourceRouteContract>& routes,
    std::string_view method,
    std::string_view path
);

std::optional<std::string> build_request_template_for_route(
    std::string_view method,
    std::string_view path,
    const std::filesystem::path& reference_repo_root
);

} // namespace tightrope::tests::contracts

