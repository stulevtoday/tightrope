#include "model_registry.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <unordered_set>

#include <curl/curl.h>
#include <glaze/glaze.hpp>

#include "text/ascii.h"

namespace tightrope::proxy::openai {

namespace {

using Json = glz::generic;
using JsonObject = Json::object_t;
constexpr std::string_view kDefaultUpstreamBaseUrl = "https://chatgpt.com/backend-api";

std::string normalize_slug(std::string_view slug) {
    return core::text::to_lower_ascii(core::text::trim_ascii(slug));
}

bool is_bootstrap_websocket_preferred(std::string_view slug_normalized) {
    return slug_normalized == "gpt-5.4" || core::text::starts_with(slug_normalized, "gpt-5.4-");
}

std::string env_or_default(const char* key, const std::string_view fallback) {
    if (key == nullptr) {
        return std::string(fallback);
    }
    const auto* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') {
        return std::string(fallback);
    }
    return std::string(raw);
}

std::string trim_trailing_slashes(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

std::optional<std::string> find_header_case_insensitive(
    const HeaderMap& headers,
    const std::string_view key
) {
    for (const auto& [candidate, value] : headers) {
        if (core::text::equals_case_insensitive(candidate, key)) {
            return value;
        }
    }
    return std::nullopt;
}

size_t write_callback(char* ptr, const size_t size, const size_t nmemb, void* userdata) {
    if (ptr == nullptr || userdata == nullptr) {
        return 0;
    }
    auto* output = static_cast<std::string*>(userdata);
    output->append(ptr, size * nmemb);
    return size * nmemb;
}

bool json_bool(const JsonObject& object, const std::string_view key, const bool default_value) {
    const auto it = object.find(std::string(key));
    if (it == object.end() || !it->second.is_boolean()) {
        return default_value;
    }
    return it->second.get_boolean();
}

std::optional<std::string> json_string(const JsonObject& object, const std::string_view key) {
    const auto it = object.find(std::string(key));
    if (it == object.end() || !it->second.is_string()) {
        return std::nullopt;
    }
    return it->second.get_string();
}

std::string models_url() {
    const auto base = trim_trailing_slashes(env_or_default("TIGHTROPE_UPSTREAM_BASE_URL", kDefaultUpstreamBaseUrl));
    const auto client_version = env_or_default("TIGHTROPE_CODEX_CLIENT_VERSION", "1.0.0");
    return base + "/codex/models?client_version=" + client_version;
}

std::optional<std::string> fetch_models_payload(const HeaderMap& inbound_headers) {
    static std::once_flag curl_once;
    std::call_once(curl_once, [] { (void)curl_global_init(CURL_GLOBAL_DEFAULT); });

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return std::nullopt;
    }

    std::string response_body;
    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");

    const auto authorization = find_header_case_insensitive(inbound_headers, "authorization");
    if (!authorization.has_value() || authorization->empty()) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return std::nullopt;
    }
    const auto normalized_auth = core::text::to_lower_ascii(core::text::trim_ascii(*authorization));
    if (core::text::starts_with(normalized_auth, "bearer sk-")) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return std::nullopt;
    }
    headers = curl_slist_append(headers, ("Authorization: " + *authorization).c_str());
    if (const auto account = find_header_case_insensitive(inbound_headers, "chatgpt-account-id");
        account.has_value() && !account->empty()) {
        headers = curl_slist_append(headers, ("chatgpt-account-id: " + *account).c_str());
    }
    if (const auto request_id = find_header_case_insensitive(inbound_headers, "x-request-id");
        request_id.has_value() && !request_id->empty()) {
        headers = curl_slist_append(headers, ("x-request-id: " + *request_id).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, models_url().c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "tightrope-native/0.1");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    const auto curl_result = curl_easy_perform(curl);
    if (curl_result != CURLE_OK) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return std::nullopt;
    }

    long status_code = 0;
    (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (status_code < 200 || status_code >= 300) {
        return std::nullopt;
    }
    return response_body;
}

std::vector<ModelInfo> parse_models_payload(const std::string& payload) {
    Json parsed;
    if (const auto ec = glz::read_json(parsed, payload); ec || !parsed.is_object()) {
        return {};
    }
    const auto& object = parsed.get_object();
    const auto models_it = object.find("models");
    if (models_it == object.end() || !models_it->second.is_array()) {
        return {};
    }

    std::vector<ModelInfo> models;
    std::unordered_set<std::string> seen_ids;
    for (const auto& raw_model : models_it->second.get_array()) {
        if (!raw_model.is_object()) {
            continue;
        }
        const auto& model_object = raw_model.get_object();
        auto id = json_string(model_object, "slug");
        if (!id.has_value() || id->empty()) {
            id = json_string(model_object, "id");
        }
        if (!id.has_value() || id->empty()) {
            continue;
        }
        const auto normalized = normalize_slug(*id);
        if (normalized.empty() || seen_ids.contains(normalized)) {
            continue;
        }
        seen_ids.insert(normalized);
        models.push_back(
            ModelInfo{
                .id = *id,
                .supports_streaming = true,
                .supported_in_api = json_bool(model_object, "supported_in_api", true),
                .prefer_websockets = json_bool(model_object, "prefer_websockets", false),
            }
        );
    }
    return models;
}

} // namespace

ModelRegistry::ModelRegistry(std::vector<ModelInfo> models) : models_(std::move(models)) {
    for (std::size_t index = 0; index < models_.size(); ++index) {
        model_index_.emplace(models_[index].id, index);
        const auto normalized = normalize_slug(models_[index].id);
        if (!normalized.empty() && !model_index_.contains(normalized)) {
            model_index_.emplace(normalized, index);
        }
    }
}

bool ModelRegistry::has_model(std::string_view id) const {
    if (model_index_.find(std::string(id)) != model_index_.end()) {
        return true;
    }
    const auto normalized = normalize_slug(id);
    if (normalized.empty()) {
        return false;
    }
    return model_index_.find(normalized) != model_index_.end();
}

const ModelInfo* ModelRegistry::find_model(std::string_view id) const {
    const auto it = model_index_.find(std::string(id));
    if (it != model_index_.end()) {
        return &models_[it->second];
    }
    const auto normalized = normalize_slug(id);
    if (normalized.empty()) {
        return nullptr;
    }
    const auto normalized_it = model_index_.find(normalized);
    if (normalized_it == model_index_.end()) {
        return nullptr;
    }
    return &models_[normalized_it->second];
}

bool ModelRegistry::prefers_websockets(std::string_view id) const {
    const auto normalized = normalize_slug(id);
    if (normalized.empty()) {
        return false;
    }

    if (const auto* model = find_model(id); model != nullptr) {
        return model->prefer_websockets;
    }
    return is_bootstrap_websocket_preferred(normalized);
}

const std::vector<ModelInfo>& ModelRegistry::list_models() const {
    return models_;
}

ModelRegistry build_default_model_registry() {
    return ModelRegistry(std::vector<ModelInfo>{});
}

ModelRegistry build_model_registry_from_upstream(const HeaderMap& inbound_headers) {
    const auto payload = fetch_models_payload(inbound_headers);
    if (!payload.has_value()) {
        return build_default_model_registry();
    }
    return ModelRegistry(parse_models_payload(*payload));
}

} // namespace tightrope::proxy::openai
