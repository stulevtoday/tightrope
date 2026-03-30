#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace tightrope::tests::contracts {

struct CapturedHttpFixture {
    std::string fixture_id;
    std::string method;
    std::string path;
    std::string auth_mode;
    std::string fixture_file;
    std::string response_model;
    std::vector<std::string> response_body_keys;
    std::string request_body;
    int success_status = 200;
    int unauthorized_status = 0;
};

std::vector<CapturedHttpFixture> capture_http_contract_fixtures(
    const std::filesystem::path& reference_repo_root,
    const std::filesystem::path& manifest_seed_path
);

void write_captured_http_contract_fixtures(
    const std::vector<CapturedHttpFixture>& fixtures,
    const std::filesystem::path& fixture_root,
    const std::filesystem::path& reference_repo_root
);

} // namespace tightrope::tests::contracts

