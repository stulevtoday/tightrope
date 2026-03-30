#include "source_contract_catalog.h"

#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "contract_io.h"
#include "contracts/internal/source_contract_internal.h"

namespace tightrope::tests::contracts {

namespace {

std::optional<std::string> extract_string_for_key(const std::string& text, const std::string& key) {
    const std::regex pattern("\"" + key + R"(\"\s*:\s*\"([^\"]+)\")");
    std::smatch match;
    if (!std::regex_search(text, match, pattern) || match.size() < 2) {
        return std::nullopt;
    }
    return match[1].str();
}

std::optional<int> extract_int_for_key(const std::string& text, const std::string& key) {
    const std::regex pattern("\"" + key + R"(\"\s*:\s*(\d+))");
    std::smatch match;
    if (!std::regex_search(text, match, pattern) || match.size() < 2) {
        return std::nullopt;
    }
    return std::stoi(match[1].str());
}

std::optional<double> extract_double_for_key(const std::string& text, const std::string& key) {
    const std::regex pattern("\"" + key + R"(\"\s*:\s*(\d+(?:\.\d+)?))");
    std::smatch match;
    if (!std::regex_search(text, match, pattern) || match.size() < 2) {
        return std::nullopt;
    }
    return std::stod(match[1].str());
}

std::optional<bool> extract_bool_for_key(const std::string& text, const std::string& key) {
    const std::regex pattern("\"" + key + R"(\"\s*:\s*(True|False|true|false))");
    std::smatch match;
    if (!std::regex_search(text, match, pattern) || match.size() < 2) {
        return std::nullopt;
    }
    return internal::to_lower_copy(match[1].str()) == "true";
}

} // namespace

std::optional<std::string> build_request_template_for_route(
    std::string_view method,
    std::string_view path,
    const std::filesystem::path& reference_repo_root
) {
    if (internal::to_upper_copy(std::string(method)) != "POST" || path != "/v1/responses") {
        return std::nullopt;
    }

    const std::string unit_requests = read_text_file(reference_repo_root / "tests" / "unit" / "test_openai_requests.py");
    const std::string integration_proxy =
        read_text_file(reference_repo_root / "tests" / "integration" / "test_proxy_responses.py");

    const auto model = extract_string_for_key(unit_requests, "model");
    const auto input = extract_string_for_key(unit_requests, "input");
    const auto stream = extract_bool_for_key(integration_proxy, "stream");
    if (!model.has_value() || !input.has_value() || !stream.has_value()) {
        throw std::runtime_error("Failed to derive /v1/responses template payload from codex-lb tests");
    }

    std::vector<std::pair<std::string, std::string>> fields;
    fields.emplace_back("model", quote_json_string(*model));
    fields.emplace_back("input", quote_json_string(*input));
    fields.emplace_back("stream", *stream ? "true" : "false");

    if (const auto value = extract_string_for_key(unit_requests, "promptCacheKey"); value.has_value()) {
        fields.emplace_back("promptCacheKey", quote_json_string(*value));
    }
    if (const auto value = extract_string_for_key(unit_requests, "promptCacheRetention"); value.has_value()) {
        fields.emplace_back("promptCacheRetention", quote_json_string(*value));
    }
    if (const auto value = extract_string_for_key(unit_requests, "service_tier"); value.has_value()) {
        fields.emplace_back("service_tier", quote_json_string(*value));
    }
    if (const auto value = extract_string_for_key(unit_requests, "reasoningEffort"); value.has_value()) {
        fields.emplace_back("reasoningEffort", quote_json_string(*value));
    }
    if (const auto value = extract_string_for_key(unit_requests, "reasoningSummary"); value.has_value()) {
        fields.emplace_back("reasoningSummary", quote_json_string(*value));
    }
    if (const auto value = extract_string_for_key(unit_requests, "textVerbosity"); value.has_value()) {
        fields.emplace_back("textVerbosity", quote_json_string(*value));
    }
    if (const auto value = extract_int_for_key(unit_requests, "max_output_tokens"); value.has_value()) {
        fields.emplace_back("max_output_tokens", std::to_string(*value));
    }
    if (const auto value = extract_double_for_key(unit_requests, "temperature"); value.has_value()) {
        std::ostringstream number;
        number << *value;
        fields.emplace_back("temperature", number.str());
    }
    if (const auto value = extract_string_for_key(unit_requests, "safety_identifier"); value.has_value()) {
        fields.emplace_back("safety_identifier", quote_json_string(*value));
    }

    std::ostringstream json;
    json << "{";
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) {
            json << ",";
        }
        json << "\"" << fields[i].first << "\":" << fields[i].second;
    }
    json << "}";
    return json.str();
}

} // namespace tightrope::tests::contracts
