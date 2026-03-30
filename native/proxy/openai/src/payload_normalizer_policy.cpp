#include "openai/internal/payload_normalizer_detail.h"

#include <stdexcept>

namespace tightrope::proxy::openai::internal {

void normalize_openai_compatible_aliases(JsonObject& payload) {
    const auto reasoning_effort = pop_string_field(payload, "reasoningEffort");
    const auto reasoning_summary = pop_string_field(payload, "reasoningSummary");
    const auto text_verbosity = pop_string_field(payload, "textVerbosity");
    const auto top_level_verbosity = pop_string_field(payload, "verbosity");
    const auto prompt_cache_key = pop_string_field(payload, "promptCacheKey");
    const auto prompt_cache_retention = pop_string_field(payload, "promptCacheRetention");

    if (prompt_cache_key.has_value() && !has_field(payload, "prompt_cache_key")) {
        payload["prompt_cache_key"] = *prompt_cache_key;
    }
    if (prompt_cache_retention.has_value() && !has_field(payload, "prompt_cache_retention")) {
        payload["prompt_cache_retention"] = *prompt_cache_retention;
    }

    Json reasoning_map = JsonObject{};
    if (const auto it = payload.find("reasoning"); it != payload.end() && it->second.is_object()) {
        reasoning_map = it->second;
    }
    if (reasoning_effort.has_value() && !reasoning_map.get_object().contains("effort")) {
        reasoning_map["effort"] = *reasoning_effort;
    }
    if (reasoning_summary.has_value() && !reasoning_map.get_object().contains("summary")) {
        reasoning_map["summary"] = *reasoning_summary;
    }
    if (!reasoning_map.get_object().empty()) {
        payload["reasoning"] = std::move(reasoning_map);
    }

    Json text_map = JsonObject{};
    if (const auto it = payload.find("text"); it != payload.end() && it->second.is_object()) {
        text_map = it->second;
    }
    if (text_verbosity.has_value() && !text_map.get_object().contains("verbosity")) {
        text_map["verbosity"] = *text_verbosity;
    }
    if (top_level_verbosity.has_value() && !text_map.get_object().contains("verbosity")) {
        text_map["verbosity"] = *top_level_verbosity;
    }
    if (!text_map.get_object().empty()) {
        payload["text"] = std::move(text_map);
    }
}

void validate_and_normalize_store(JsonObject& payload) {
    auto it = payload.find("store");
    if (it == payload.end()) {
        payload["store"] = false;
        return;
    }
    if (it->second.is_null()) {
        it->second = false;
        return;
    }
    if (!it->second.is_boolean() || it->second.get_boolean()) {
        throw std::runtime_error("store must be false");
    }
}

void validate_and_normalize_include(JsonObject& payload) {
    const auto include_it = payload.find("include");
    if (include_it == payload.end()) {
        return;
    }
    if (!include_it->second.is_array()) {
        throw std::runtime_error("Unsupported include value");
    }
    for (const auto& value : include_it->second.get_array()) {
        if (!value.is_string() || !contains_string(kResponsesIncludeAllowlist, value.get_string())) {
            throw std::runtime_error("Unsupported include value");
        }
    }
}

void validate_and_normalize_tools(JsonObject& payload) {
    auto tools_it = payload.find("tools");
    if (tools_it == payload.end()) {
        return;
    }
    if (!tools_it->second.is_array()) {
        throw std::runtime_error("Unsupported tool type");
    }

    Json normalized_tools = JsonArray{};
    for (const auto& tool : tools_it->second.get_array()) {
        if (!tool.is_object()) {
            normalized_tools.get_array().push_back(tool);
            continue;
        }
        Json normalized_tool = tool;
        auto& tool_object = normalized_tool.get_object();
        const auto type_it = tool_object.find("type");
        if (type_it != tool_object.end() && type_it->second.is_string()) {
            const auto normalized_type = normalize_tool_type_alias(type_it->second.get_string());
            if (contains_string(kUnsupportedToolTypes, normalized_type)) {
                throw std::runtime_error("Unsupported tool type");
            }
            type_it->second = normalized_type;
        }
        normalized_tools.get_array().push_back(std::move(normalized_tool));
    }
    tools_it->second = std::move(normalized_tools);

    const auto choice_it = payload.find("tool_choice");
    if (choice_it == payload.end() || !choice_it->second.is_object()) {
        return;
    }
    auto& choice_object = choice_it->second.get_object();
    const auto type_it = choice_object.find("type");
    if (type_it != choice_object.end() && type_it->second.is_string()) {
        type_it->second = normalize_tool_type_alias(type_it->second.get_string());
    }
}

void validate_and_normalize_responses_fields(JsonObject& payload) {
    const auto truncation_it = payload.find("truncation");
    if (truncation_it != payload.end()) {
        if (!truncation_it->second.is_null()) {
            throw std::runtime_error("truncation is not supported");
        }
        payload.erase(truncation_it);
    }

    auto previous_response_it = payload.find("previous_response_id");
    if (previous_response_it != payload.end()) {
        if (previous_response_it->second.is_null()) {
            payload.erase(previous_response_it);
        } else if (!previous_response_it->second.is_string()) {
            throw std::runtime_error("previous_response_id must be a string");
        } else {
            const auto trimmed = core::text::trim_ascii(previous_response_it->second.get_string());
            if (trimmed.empty()) {
                payload.erase(previous_response_it);
            } else {
                previous_response_it->second = trimmed;
            }
        }
    }

    const auto conversation_it = payload.find("conversation");
    previous_response_it = payload.find("previous_response_id");
    if (conversation_it != payload.end() && previous_response_it != payload.end() && conversation_it->second.is_string() &&
        previous_response_it->second.is_string() && !conversation_it->second.get_string().empty() &&
        !previous_response_it->second.get_string().empty()) {
        throw std::runtime_error("Provide either 'conversation' or 'previous_response_id', not both.");
    }
}

} // namespace tightrope::proxy::openai::internal
