#include "payload_normalizer.h"

#include <stdexcept>

#include <glaze/glaze.hpp>

#include "openai/internal/payload_normalizer_detail.h"

namespace tightrope::proxy::openai {

namespace {

using Json = glz::generic;
using JsonObject = Json::object_t;

NormalizedRequest normalize_with_strategy(const std::string& raw_request_body, const bool compact_mode) {
    Json payload;
    if (auto ec = glz::read_json(payload, raw_request_body); ec) {
        throw std::runtime_error("request payload is not valid JSON");
    }
    if (!payload.is_object()) {
        throw std::runtime_error("request payload must be a JSON object");
    }

    auto& object = payload.get_object();
    internal::normalize_request_base_fields(object, compact_mode);
    internal::normalize_openai_compatible_aliases(object);
    internal::validate_and_normalize_store(object);
    if (!compact_mode) {
        internal::validate_and_normalize_include(object);
        internal::validate_and_normalize_tools(object);
        internal::validate_and_normalize_responses_fields(object);
    }
    if (const auto it = object.find("service_tier"); it != object.end() && it->second.is_string()) {
        it->second = internal::normalize_service_tier_alias(it->second.get_string());
    }

    for (const auto field : internal::kUnsupportedUpstreamFields) {
        object.erase(std::string(field));
    }
    if (compact_mode) {
        object.erase("store");
    }

    const auto serialized = glz::write_json(payload);
    if (!serialized) {
        throw std::runtime_error("failed to serialize normalized request payload");
    }
    return NormalizedRequest{.body = serialized.value_or("{}")};
}

} // namespace

NormalizedRequest normalize_request(const std::string& raw_request_body) {
    return normalize_with_strategy(raw_request_body, /*compact_mode=*/false);
}

NormalizedRequest normalize_compact_request(const std::string& raw_request_body) {
    return normalize_with_strategy(raw_request_body, /*compact_mode=*/true);
}

} // namespace tightrope::proxy::openai
