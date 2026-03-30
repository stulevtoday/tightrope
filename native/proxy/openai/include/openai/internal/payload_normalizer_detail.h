#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include <glaze/glaze.hpp>

#include "text/ascii.h"

namespace tightrope::proxy::openai::internal {

using Json = glz::generic;
using JsonArray = Json::array_t;
using JsonObject = Json::object_t;

inline constexpr std::array<std::string_view, 4> kUnsupportedUpstreamFields = {
    "max_output_tokens",
    "prompt_cache_retention",
    "safety_identifier",
    "temperature",
};

inline constexpr std::array<std::string_view, 7> kResponsesIncludeAllowlist = {
    "code_interpreter_call.outputs",
    "computer_call_output.output.image_url",
    "file_search_call.results",
    "message.input_image.image_url",
    "message.output_text.logprobs",
    "reasoning.encrypted_content",
    "web_search_call.action.sources",
};

inline constexpr std::array<std::string_view, 5> kUnsupportedToolTypes = {
    "file_search",
    "code_interpreter",
    "computer_use",
    "computer_use_preview",
    "image_generation",
};

inline constexpr std::array<std::string_view, 4> kInterleavedReasoningKeys = {
    "reasoning_content",
    "reasoning_details",
    "tool_calls",
    "function_call",
};

inline constexpr std::array<std::string_view, 3> kInterleavedReasoningPartTypes = {
    "reasoning",
    "reasoning_content",
    "reasoning_details",
};

inline constexpr std::array<std::string_view, 3> kAssistantTextPartTypes = {
    "text",
    "input_text",
    "output_text",
};

inline constexpr std::array<std::string_view, 4> kToolTextPartTypes = {
    "text",
    "input_text",
    "output_text",
    "refusal",
};

template <std::size_t N>
inline bool contains_string(const std::array<std::string_view, N>& values, const std::string_view candidate) {
    for (const auto value : values) {
        if (value == candidate) {
            return true;
        }
    }
    return false;
}

inline bool has_field(const JsonObject& object, const std::string_view key) {
    return object.find(std::string(key)) != object.end();
}

inline std::optional<Json> pop_field(JsonObject& object, const std::string_view key) {
    const auto it = object.find(std::string(key));
    if (it == object.end()) {
        return std::nullopt;
    }
    Json value = std::move(it->second);
    object.erase(it);
    return value;
}

inline std::optional<std::string> pop_string_field(JsonObject& object, const std::string_view key) {
    auto value = pop_field(object, key);
    if (!value.has_value() || !value->is_string()) {
        return std::nullopt;
    }
    return value->get_string();
}

inline std::optional<std::string> json_string_field(const JsonObject& object, const std::string_view key) {
    const auto it = object.find(std::string(key));
    if (it == object.end() || !it->second.is_string()) {
        return std::nullopt;
    }
    return it->second.get_string();
}

inline std::optional<std::string> serialize_json_value(const Json& value) {
    const auto serialized = glz::write_json(value);
    if (!serialized) {
        return std::nullopt;
    }
    return serialized.value_or("");
}

inline std::string normalize_service_tier_alias(const std::string& service_tier) {
    if (core::text::equals_case_insensitive(core::text::trim_ascii(service_tier), "fast")) {
        return "priority";
    }
    return service_tier;
}

inline std::string normalize_tool_type_alias(const std::string& tool_type) {
    if (tool_type == "web_search_preview") {
        return "web_search";
    }
    return tool_type;
}

void normalize_request_base_fields(JsonObject& payload, bool compact_mode);
void normalize_openai_compatible_aliases(JsonObject& payload);
void validate_and_normalize_store(JsonObject& payload);
void validate_and_normalize_include(JsonObject& payload);
void validate_and_normalize_tools(JsonObject& payload);
void validate_and_normalize_responses_fields(JsonObject& payload);

} // namespace tightrope::proxy::openai::internal
