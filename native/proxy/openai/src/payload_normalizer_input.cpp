#include "openai/internal/payload_normalizer_detail.h"

#include <stdexcept>
#include <string>

namespace tightrope::proxy::openai::internal {

namespace {

Json output_text_part(const std::string& text) {
    Json part = JsonObject{};
    part["type"] = "output_text";
    part["text"] = text;
    return part;
}

Json normalized_input_text(const std::string& text) {
    Json text_part = JsonObject{};
    text_part["type"] = "input_text";
    text_part["text"] = text;

    Json content = JsonArray{};
    content.get_array().push_back(std::move(text_part));

    Json user_input_item = JsonObject{};
    user_input_item["role"] = "user";
    user_input_item["content"] = std::move(content);

    Json normalized_input = JsonArray{};
    normalized_input.get_array().push_back(std::move(user_input_item));
    return normalized_input;
}

bool is_input_file_with_id(const JsonObject& object) {
    const auto type = json_string_field(object, "type");
    if (!type.has_value() || *type != "input_file") {
        return false;
    }
    const auto file_id = json_string_field(object, "file_id");
    return file_id.has_value() && !file_id->empty();
}

bool has_input_file_id(const JsonArray& input_items) {
    for (const auto& item : input_items) {
        if (!item.is_object()) {
            continue;
        }
        const auto& item_object = item.get_object();
        if (is_input_file_with_id(item_object)) {
            return true;
        }

        const auto content_it = item_object.find("content");
        if (content_it == item_object.end()) {
            continue;
        }
        if (content_it->second.is_array()) {
            for (const auto& part : content_it->second.get_array()) {
                if (part.is_object() && is_input_file_with_id(part.get_object())) {
                    return true;
                }
            }
        } else if (content_it->second.is_object() && is_input_file_with_id(content_it->second.get_object())) {
            return true;
        }
    }
    return false;
}

std::optional<Json> sanitize_interleaved_reasoning_content_part(const Json& part) {
    if (!part.is_object()) {
        return part;
    }
    const auto& part_object = part.get_object();
    const auto part_type = json_string_field(part_object, "type");
    if (part_type.has_value() && contains_string(kInterleavedReasoningPartTypes, *part_type)) {
        return std::nullopt;
    }

    Json sanitized = part;
    auto& sanitized_object = sanitized.get_object();
    for (const auto key : kInterleavedReasoningKeys) {
        sanitized_object.erase(std::string(key));
    }
    return sanitized;
}

std::optional<Json> sanitize_interleaved_reasoning_content(const Json& content) {
    if (content.is_array()) {
        Json sanitized = JsonArray{};
        auto& parts = sanitized.get_array();
        for (const auto& part : content.get_array()) {
            const auto sanitized_part = sanitize_interleaved_reasoning_content_part(part);
            if (sanitized_part.has_value()) {
                parts.push_back(*sanitized_part);
            }
        }
        return sanitized;
    }
    if (content.is_object()) {
        return sanitize_interleaved_reasoning_content_part(content);
    }
    return content;
}

template <std::size_t N>
std::optional<std::string> extract_text_content_part(
    const Json& part,
    const std::array<std::string_view, N>& allowed_types,
    const bool allow_refusal = false
) {
    if (!part.is_object()) {
        return std::nullopt;
    }
    const auto& part_object = part.get_object();
    const auto type = json_string_field(part_object, "type");
    const auto text = json_string_field(part_object, "text");
    if (text.has_value() && (!type.has_value() || contains_string(allowed_types, *type))) {
        return text;
    }
    if (allow_refusal && type.has_value() && *type == "refusal") {
        return json_string_field(part_object, "refusal");
    }
    return std::nullopt;
}

Json normalize_assistant_content(const Json& content) {
    if (content.is_string()) {
        Json normalized = JsonArray{};
        normalized.get_array().push_back(output_text_part(content.get_string()));
        return normalized;
    }
    if (content.is_array()) {
        Json normalized = JsonArray{};
        auto& parts = normalized.get_array();
        for (const auto& part : content.get_array()) {
            if (part.is_string()) {
                parts.push_back(output_text_part(part.get_string()));
                continue;
            }
            const auto text = extract_text_content_part(part, kAssistantTextPartTypes);
            if (text.has_value()) {
                parts.push_back(output_text_part(*text));
            } else {
                parts.push_back(part);
            }
        }
        return normalized;
    }
    if (content.is_object()) {
        Json normalized = JsonArray{};
        const auto text = extract_text_content_part(content, kAssistantTextPartTypes);
        normalized.get_array().push_back(text.has_value() ? output_text_part(*text) : content);
        return normalized;
    }
    return content;
}

std::optional<std::string> resolve_tool_call_id(const JsonObject& object) {
    if (const auto value = json_string_field(object, "tool_call_id"); value.has_value() && !value->empty()) {
        return value;
    }
    if (const auto value = json_string_field(object, "toolCallId"); value.has_value() && !value->empty()) {
        return value;
    }
    if (const auto value = json_string_field(object, "call_id"); value.has_value() && !value->empty()) {
        return value;
    }
    return std::nullopt;
}

std::string normalize_tool_output_value(const Json& content) {
    if (content.is_null()) {
        return "";
    }
    if (content.is_string()) {
        return content.get_string();
    }
    if (content.is_array()) {
        std::string output;
        for (const auto& part : content.get_array()) {
            if (part.is_string()) {
                output += part.get_string();
                continue;
            }
            const auto text = extract_text_content_part(part, kToolTextPartTypes, true);
            if (text.has_value()) {
                output += *text;
            }
        }
        if (!output.empty()) {
            return output;
        }
        return serialize_json_value(content).value_or("");
    }
    if (content.is_object()) {
        const auto text = extract_text_content_part(content, kToolTextPartTypes, true);
        if (text.has_value()) {
            return *text;
        }
        return serialize_json_value(content).value_or("");
    }
    return serialize_json_value(content).value_or("");
}

Json normalize_role_input_item(const Json& item) {
    if (!item.is_object()) {
        return item;
    }

    const auto& item_object = item.get_object();
    const auto role = json_string_field(item_object, "role");
    if (!role.has_value()) {
        return item;
    }
    if (*role == "assistant") {
        Json normalized = item;
        auto& normalized_object = normalized.get_object();
        const auto content_it = normalized_object.find("content");
        if (content_it != normalized_object.end()) {
            content_it->second = normalize_assistant_content(content_it->second);
        }
        return normalized;
    }
    if (*role != "tool") {
        return item;
    }

    const auto call_id = resolve_tool_call_id(item_object);
    if (!call_id.has_value()) {
        throw std::runtime_error("tool input items must include 'tool_call_id'");
    }

    const auto output_it = item_object.find("output");
    const auto content_it = item_object.find("content");
    const Json* output_source = nullptr;
    if (output_it != item_object.end() && !output_it->second.is_null()) {
        output_source = &output_it->second;
    } else if (content_it != item_object.end()) {
        output_source = &content_it->second;
    }

    Json normalized = JsonObject{};
    normalized["type"] = "function_call_output";
    normalized["call_id"] = *call_id;
    normalized["output"] = output_source == nullptr ? "" : normalize_tool_output_value(*output_source);
    return normalized;
}

Json sanitize_input_items(const JsonArray& input_items) {
    Json sanitized_input = JsonArray{};
    auto& sanitized_items = sanitized_input.get_array();
    for (const auto& item : input_items) {
        if (!item.is_object()) {
            sanitized_items.push_back(normalize_role_input_item(item));
            continue;
        }

        Json sanitized_item = JsonObject{};
        for (const auto& [key, value] : item.get_object()) {
            if (contains_string(kInterleavedReasoningKeys, key)) {
                continue;
            }
            if (key == "content") {
                const auto sanitized_content = sanitize_interleaved_reasoning_content(value);
                if (sanitized_content.has_value()) {
                    sanitized_item[key] = *sanitized_content;
                }
                continue;
            }
            sanitized_item[key] = value;
        }
        sanitized_items.push_back(normalize_role_input_item(sanitized_item));
    }
    return sanitized_input;
}

void normalize_input_field(JsonObject& payload) {
    auto input_it = payload.find("input");
    if (input_it == payload.end()) {
        throw std::runtime_error("request payload missing required field: input");
    }
    if (input_it->second.is_string()) {
        payload["input"] = normalized_input_text(input_it->second.get_string());
        input_it = payload.find("input");
    } else if (!input_it->second.is_array()) {
        throw std::runtime_error("request payload field 'input' must be a string or array");
    }

    if (has_input_file_id(input_it->second.get_array())) {
        throw std::runtime_error("input_file.file_id is not supported");
    }
    input_it->second = sanitize_input_items(input_it->second.get_array());
}

} // namespace

void normalize_request_base_fields(JsonObject& payload, const bool compact_mode) {
    const auto model_it = payload.find("model");
    if (model_it == payload.end() || !model_it->second.is_string() || model_it->second.get_string().empty()) {
        throw std::runtime_error("request payload missing required field: model");
    }

    normalize_input_field(payload);

    const auto instructions_it = payload.find("instructions");
    if (instructions_it == payload.end() || !instructions_it->second.is_string()) {
        payload["instructions"] = "";
    }
    if (!compact_mode && !has_field(payload, "tools")) {
        payload["tools"] = JsonArray{};
    }
    if (!compact_mode && !has_field(payload, "include")) {
        payload["include"] = JsonArray{};
    }
    if (!has_field(payload, "store")) {
        payload["store"] = false;
    }
}

} // namespace tightrope::proxy::openai::internal
