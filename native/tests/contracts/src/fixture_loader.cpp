#include "fixture_loader.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#include "contract_io.h"

namespace tightrope::tests::contracts {

namespace {

const std::filesystem::path kDefaultManifestPath = "native/tests/contracts/fixtures/http/manifest.json";

std::string escape_regex(const std::string& value) {
    static const std::regex meta(R"([.^$|()\\[\]{}*+?])");
    return std::regex_replace(value, meta, R"(\$&)");
}

std::size_t skip_ws(const std::string& text, std::size_t pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
        ++pos;
    }
    return pos;
}

std::optional<std::string> extract_json_string(const std::string& text, const std::string& key) {
    const std::regex pattern("\"" + escape_regex(key) + R"(\"\s*:\s*\"([^\"]*)\")");
    std::smatch match;
    if (!std::regex_search(text, match, pattern) || match.size() < 2) {
        return std::nullopt;
    }
    return match[1].str();
}

std::optional<int> extract_json_int(const std::string& text, const std::string& key) {
    const std::regex pattern("\"" + escape_regex(key) + R"(\"\s*:\s*(-?\d+))");
    std::smatch match;
    if (!std::regex_search(text, match, pattern) || match.size() < 2) {
        return std::nullopt;
    }
    return std::stoi(match[1].str());
}

std::string require_json_string(const std::string& text, const std::string& key) {
    const auto value = extract_json_string(text, key);
    if (!value.has_value()) {
        throw std::runtime_error("Missing string key in JSON: " + key);
    }
    return *value;
}

std::optional<std::string> extract_json_object(const std::string& text, const std::string& key) {
    const std::string quoted_key = "\"" + key + "\"";
    const std::size_t key_pos = text.find(quoted_key);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }

    const std::size_t colon_pos = text.find(':', key_pos + quoted_key.size());
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }

    const std::size_t object_start = skip_ws(text, colon_pos + 1);
    if (object_start >= text.size() || text[object_start] != '{') {
        return std::nullopt;
    }

    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t i = object_start; i < text.size(); ++i) {
        const char ch = text[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (ch == '"') {
                in_string = false;
            }
            continue;
        }

        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '{') {
            ++depth;
            continue;
        }
        if (ch == '}') {
            --depth;
            if (depth == 0) {
                return text.substr(object_start, i - object_start + 1);
            }
        }
    }
    return std::nullopt;
}

std::string unescape_json_string(const std::string& value) {
    std::string unescaped;
    unescaped.reserve(value.size());
    bool escaping = false;
    for (const char ch : value) {
        if (!escaping) {
            if (ch == '\\') {
                escaping = true;
                continue;
            }
            unescaped.push_back(ch);
            continue;
        }

        switch (ch) {
        case '"':
            unescaped.push_back('"');
            break;
        case '\\':
            unescaped.push_back('\\');
            break;
        case 'n':
            unescaped.push_back('\n');
            break;
        case 'r':
            unescaped.push_back('\r');
            break;
        case 't':
            unescaped.push_back('\t');
            break;
        default:
            unescaped.push_back(ch);
            break;
        }
        escaping = false;
    }
    return unescaped;
}

int require_fixture_status(const std::string& fixture_json) {
    if (const auto status = extract_json_int(fixture_json, "status"); status.has_value()) {
        return *status;
    }
    if (const auto status = extract_json_int(fixture_json, "success_status"); status.has_value()) {
        return *status;
    }
    throw std::runtime_error("Fixture is missing status/success_status");
}

std::string upper_ascii(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::vector<HttpFixtureManifestEntry> parse_manifest_entries(
    const std::filesystem::path& manifest_path,
    const std::string& manifest_text
) {
    const auto fixtures_object = extract_json_object(manifest_text, "fixtures");
    if (!fixtures_object.has_value()) {
        throw std::runtime_error("Manifest is missing fixtures object: " + manifest_path.string());
    }

    const std::regex entry_pattern("\"([^\"]+)\"\\s*:\\s*\\{([^{}]*)\\}");
    std::vector<HttpFixtureManifestEntry> entries;
    for (std::sregex_iterator it(fixtures_object->begin(), fixtures_object->end(), entry_pattern), end; it != end;
         ++it) {
        const std::string fixture_id = (*it)[1].str();
        const std::string manifest_entry = (*it)[2].str();

        HttpFixtureManifestEntry entry;
        entry.id = fixture_id;
        entry.method = require_json_string(manifest_entry, "method");
        entry.path = require_json_string(manifest_entry, "path");
        entry.auth_mode = require_json_string(manifest_entry, "auth_mode");
        entry.fixture_path = manifest_path.parent_path() / require_json_string(manifest_entry, "fixture_path");
        if (const auto response_model = extract_json_string(manifest_entry, "response_model"); response_model.has_value()) {
            entry.response_model = *response_model;
        }
        entries.push_back(std::move(entry));
    }

    return entries;
}

} // namespace

std::vector<HttpFixtureManifestEntry> load_http_fixture_manifest() {
    return load_http_fixture_manifest(kDefaultManifestPath);
}

std::vector<HttpFixtureManifestEntry> load_http_fixture_manifest(const std::filesystem::path& manifest_path) {
    const std::string manifest = read_text_file(manifest_path);
    return parse_manifest_entries(manifest_path, manifest);
}

HttpFixture load_http_fixture(const std::string& fixture_id) {
    const auto entries = load_http_fixture_manifest();
    const auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
        return entry.id == fixture_id;
    });
    if (it == entries.end()) {
        throw std::runtime_error("Fixture id not found in manifest: " + fixture_id);
    }

    const std::string fixture_json = read_text_file(it->fixture_path);
    HttpFixture fixture;
    fixture.id = fixture_id;
    fixture.method = it->method;
    fixture.path = it->path;
    fixture.auth_mode = it->auth_mode;
    fixture.fixture_path = it->fixture_path;
    if (const auto request_object = extract_json_object(fixture_json, "request"); request_object.has_value()) {
        if (const auto request_body = extract_json_object(*request_object, "body"); request_body.has_value()) {
            fixture.request.body = *request_body;
        }
    }
    if (fixture.request.body.empty()) {
        if (const auto request_body = extract_json_string(fixture_json, "request_body"); request_body.has_value()) {
            fixture.request.body = unescape_json_string(*request_body);
        }
    }
    fixture.response.status = require_fixture_status(fixture_json);
    fixture.response.body = fixture_json;
    return fixture;
}

HttpFixture load_http_fixture_by_route(const std::string& method, const std::string& path) {
    const std::string normalized_method = upper_ascii(method);
    const auto entries = load_http_fixture_manifest();
    const auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
        return upper_ascii(entry.method) == normalized_method && entry.path == path;
    });
    if (it == entries.end()) {
        throw std::runtime_error("Fixture route not found in manifest: " + method + " " + path);
    }
    return load_http_fixture(it->id);
}

std::vector<std::string> load_ndjson_fixture(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open NDJSON fixture: " + path.string());
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

} // namespace tightrope::tests::contracts
