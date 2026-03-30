#include "http_fixture_capture.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "contract_io.h"
#include "fixture_loader.h"
#include "source_contract_catalog.h"

namespace tightrope::tests::contracts {

namespace {

std::string body_keys_array_json(const std::vector<std::string>& keys) {
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << quote_json_string(keys[i]);
    }
    out << "]";
    return out.str();
}

std::string fixture_json_text(const CapturedHttpFixture& fixture) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"fixture_id\": " << quote_json_string(fixture.fixture_id) << ",\n";
    out << "  \"source\": \"codex-lb-source-contracts\",\n";
    out << "  \"request\": {\n";
    out << "    \"method\": " << quote_json_string(fixture.method) << ",\n";
    out << "    \"path\": " << quote_json_string(fixture.path) << ",\n";
    out << "    \"auth_mode\": " << quote_json_string(fixture.auth_mode);
    if (!fixture.request_body.empty()) {
        out << ",\n    \"body\": " << fixture.request_body << "\n";
    } else {
        out << "\n";
    }
    out << "  },\n";
    out << "  \"response_contract\": {\n";
    out << "    \"success_status\": " << fixture.success_status;
    if (fixture.unauthorized_status > 0) {
        out << ",\n    \"unauthorized_status\": " << fixture.unauthorized_status;
    }
    if (!fixture.response_model.empty()) {
        out << ",\n    \"response_model\": " << quote_json_string(fixture.response_model);
    }
    out << ",\n    \"body_keys\": " << body_keys_array_json(fixture.response_body_keys) << "\n";
    out << "  }\n";
    out << "}\n";
    return out.str();
}

std::string manifest_json_text(
    const std::vector<CapturedHttpFixture>& fixtures,
    const std::filesystem::path& reference_repo_root
) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"format_version\": 2,\n";
    out << "  \"capture_mode\": \"source-derived-from-codex-lb\",\n";
    out << "  \"source_repo\": " << quote_json_string(reference_repo_root.string()) << ",\n";
    out << "  \"fixtures\": {\n";
    for (std::size_t i = 0; i < fixtures.size(); ++i) {
        const auto& fixture = fixtures[i];
        out << "    " << quote_json_string(fixture.fixture_id) << ": {\n";
        out << "      \"method\": " << quote_json_string(fixture.method) << ",\n";
        out << "      \"path\": " << quote_json_string(fixture.path) << ",\n";
        out << "      \"auth_mode\": " << quote_json_string(fixture.auth_mode) << ",\n";
        out << "      \"fixture_path\": " << quote_json_string(fixture.fixture_file);
        if (!fixture.response_model.empty()) {
            out << ",\n      \"response_model\": " << quote_json_string(fixture.response_model) << "\n";
        } else {
            out << "\n";
        }
        out << "    }";
        if (i + 1 < fixtures.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  }\n";
    out << "}\n";
    return out.str();
}

} // namespace

std::vector<CapturedHttpFixture> capture_http_contract_fixtures(
    const std::filesystem::path& reference_repo_root,
    const std::filesystem::path& manifest_seed_path
) {
    const auto source_routes = load_source_route_contracts(reference_repo_root);
    auto seed_fixtures = load_http_fixture_manifest(manifest_seed_path);
    std::sort(seed_fixtures.begin(), seed_fixtures.end(), [](const auto& a, const auto& b) {
        return a.id < b.id;
    });

    std::vector<CapturedHttpFixture> captured;
    captured.reserve(seed_fixtures.size());
    for (const auto& seed : seed_fixtures) {
        const auto source = find_source_route_contract(source_routes, seed.method, seed.path);
        if (!source.has_value()) {
            throw std::runtime_error("Seed route not found in codex-lb source: " + seed.method + " " + seed.path);
        }
        const auto request_template = build_request_template_for_route(seed.method, seed.path, reference_repo_root);
        captured.push_back(CapturedHttpFixture{
            .fixture_id = seed.id,
            .method = seed.method,
            .path = seed.path,
            .auth_mode = source->auth_mode,
            .fixture_file = seed.fixture_path.filename().string(),
            .response_model = source->response_model,
            .response_body_keys = source->response_body_keys,
            .request_body = request_template.value_or(""),
            .success_status = 200,
            .unauthorized_status = source->auth_mode == "none" ? 0 : 401,
        });
    }
    return captured;
}

void write_captured_http_contract_fixtures(
    const std::vector<CapturedHttpFixture>& fixtures,
    const std::filesystem::path& fixture_root,
    const std::filesystem::path& reference_repo_root
) {
    for (const auto& fixture : fixtures) {
        write_text_file(fixture_root / fixture.fixture_file, fixture_json_text(fixture));
    }
    write_text_file(fixture_root / "manifest.json", manifest_json_text(fixtures, reference_repo_root));
}

} // namespace tightrope::tests::contracts
