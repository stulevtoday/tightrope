#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace tightrope::tests::contracts {

struct HttpFixtureManifestEntry {
    std::string id;
    std::string method;
    std::string path;
    std::string auth_mode;
    std::filesystem::path fixture_path;
    std::string response_model;
};

struct HttpFixtureResponse {
    int status = 0;
    std::string body;
};

struct HttpFixtureRequest {
    std::string body;
};

struct HttpFixture {
    std::string id;
    std::string method;
    std::string path;
    std::string auth_mode;
    std::filesystem::path fixture_path;
    HttpFixtureRequest request;
    HttpFixtureResponse response;
};

std::vector<HttpFixtureManifestEntry> load_http_fixture_manifest();
std::vector<HttpFixtureManifestEntry> load_http_fixture_manifest(const std::filesystem::path& manifest_path);

HttpFixture load_http_fixture(const std::string& fixture_id);
HttpFixture load_http_fixture_by_route(const std::string& method, const std::string& path);

std::vector<std::string> load_ndjson_fixture(const std::filesystem::path& path);

} // namespace tightrope::tests::contracts
