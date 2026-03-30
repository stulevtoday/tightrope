#include "chat_completions_service.h"

#include <array>
#include <cctype>
#include <ctime>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glaze/glaze.hpp>

#include "openai/error_envelope.h"
#include "openai/internal/payload_normalizer_detail.h"
#include "proxy_service.h"
#include "text/ascii.h"

namespace tightrope::proxy {

namespace {

using Json = glz::generic;
using JsonArray = Json::array_t;
using JsonObject = Json::object_t;

constexpr std::array<std::string_view, 5> kSupportedRoles = {
    "system",
    "developer",
    "user",
    "assistant",
    "tool",
};

constexpr std::size_t kMaxImageDataUrlBytes = 8U * 1024U * 1024U;

class ChatValidationError final : public std::runtime_error {
public:
    ChatValidationError(std::string message, std::string_view param = "")
        : std::runtime_error(std::move(message)), param_(param) {}

    [[nodiscard]] const std::string& param() const noexcept {
        return param_;
    }

private:
    std::string param_;
};

bool is_supported_role(const std::string_view role) {
    for (const auto candidate : kSupportedRoles) {
        if (role == candidate) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> serialize_json(const Json& payload) {
    const auto serialized = glz::write_json(payload);
    if (!serialized) {
        return std::nullopt;
    }
    return serialized.value_or("{}");
}

std::optional<std::string> json_string(const JsonObject& object, const std::string_view key) {
    const auto it = object.find(std::string(key));
    if (it == object.end() || !it->second.is_string()) {
        return std::nullopt;
    }
    return it->second.get_string();
}

bool json_bool(const JsonObject& object, const std::string_view key, const bool default_value = false) {
    const auto it = object.find(std::string(key));
    if (it == object.end() || !it->second.is_boolean()) {
        return default_value;
    }
    return it->second.get_boolean();
}

Json input_text_part(const std::string& text) {
    Json part = JsonObject{};
    part["type"] = "input_text";
    part["text"] = text;
    return part;
}

std::string infer_content_part_type(const JsonObject& object) {
    const auto explicit_type = json_string(object, "type");
    if (explicit_type.has_value() && !explicit_type->empty()) {
        return *explicit_type;
    }
    if (json_string(object, "text").has_value()) {
        return "text";
    }
    return {};
}

bool is_valid_json_schema_name(const std::string_view name) {
    if (name.empty() || name.size() > 64) {
        return false;
    }
    for (const auto ch : name) {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-') {
            continue;
        }
        return false;
    }
    return true;
}

bool is_oversized_data_url(const std::string_view url) {
    if (!url.starts_with("data:")) {
        return false;
    }
    const auto comma = url.find(',');
    if (comma == std::string_view::npos) {
        return false;
    }
    const auto header = url.substr(0, comma);
    if (header.find(";base64") == std::string_view::npos) {
        return false;
    }
    const auto data = url.substr(comma + 1);
    std::size_t padding = 0;
    while (padding < data.size() && data[data.size() - 1U - padding] == '=') {
        ++padding;
    }
    const auto size = (data.size() * 3U) / 4U;
    const auto decoded_bytes = size > padding ? size - padding : 0U;
    return decoded_bytes > kMaxImageDataUrlBytes;
}

void ensure_text_only_content(const Json& content, const std::string_view role) {
    const auto fail = [role]() -> ChatValidationError {
        return ChatValidationError(std::string(role) + " messages must be text-only.", "messages");
    };

    if (content.is_null() || content.is_string()) {
        return;
    }
    if (content.is_array()) {
        for (const auto& part : content.get_array()) {
            if (part.is_string()) {
                continue;
            }
            if (!part.is_object()) {
                throw fail();
            }
            const auto& object = part.get_object();
            const auto type = infer_content_part_type(object);
            if (!type.empty() && type != "text") {
                throw fail();
            }
            if (!json_string(object, "text").has_value()) {
                throw fail();
            }
        }
        return;
    }
    if (content.is_object()) {
        const auto& object = content.get_object();
        const auto type = infer_content_part_type(object);
        if (!type.empty() && type != "text") {
            throw fail();
        }
        if (json_string(object, "text").has_value()) {
            return;
        }
    }
    throw fail();
}

std::optional<std::string> optional_tool_call_id(const JsonObject& object) {
    return json_string(object, "tool_call_id")
        .or_else([&]() { return json_string(object, "toolCallId"); })
        .or_else([&]() { return json_string(object, "call_id"); });
}

std::string normalize_tool_output_content(const Json& content) {
    if (content.is_string()) {
        return content.get_string();
    }
    if (!content.is_array()) {
        throw ChatValidationError("tool message content must be a string or array.", "messages");
    }

    std::string output;
    for (const auto& part : content.get_array()) {
        if (part.is_string()) {
            output += part.get_string();
            continue;
        }
        if (!part.is_object()) {
            continue;
        }
        const auto text = json_string(part.get_object(), "text");
        if (text.has_value()) {
            output += *text;
        }
    }
    if (output.empty() && !content.get_array().empty()) {
        throw ChatValidationError("tool message content array contains no valid text parts.", "messages");
    }
    return output;
}

Json map_chat_file_part(const JsonObject& object) {
    const auto file_it = object.find("file");
    if (file_it == object.end() || !file_it->second.is_object()) {
        throw ChatValidationError("File content parts must include file metadata.", "messages");
    }
    const auto& file = file_it->second.get_object();
    if (const auto file_id = json_string(file, "file_id"); file_id.has_value() && !file_id->empty()) {
        throw ChatValidationError("Invalid request payload", "messages");
    }

    Json mapped = JsonObject{};
    mapped["type"] = "input_file";

    if (const auto file_url = json_string(file, "file_url"); file_url.has_value() && !file_url->empty()) {
        mapped["file_url"] = *file_url;
        return mapped;
    }

    auto file_data = json_string(file, "file_data");
    if (!file_data.has_value() || file_data->empty()) {
        file_data = json_string(file, "data");
    }
    if (file_data.has_value() && !file_data->empty()) {
        auto mime_type = json_string(file, "mime_type");
        if (!mime_type.has_value() || mime_type->empty()) {
            mime_type = json_string(file, "content_type");
        }
        const std::string resolved_mime = mime_type.has_value() && !mime_type->empty() ? *mime_type : "application/octet-stream";
        mapped["file_url"] = "data:" + resolved_mime + ";base64," + *file_data;
    }
    return mapped;
}

std::optional<Json> map_chat_image_part(const JsonObject& object) {
    const auto image_it = object.find("image_url");
    std::optional<std::string> url;
    std::optional<std::string> detail;
    if (image_it != object.end() && image_it->second.is_object()) {
        url = json_string(image_it->second.get_object(), "url");
        detail = json_string(image_it->second.get_object(), "detail");
    } else if (image_it != object.end() && image_it->second.is_string()) {
        url = image_it->second.get_string();
    }
    if (!url.has_value() || url->empty()) {
        throw ChatValidationError("Image content parts must include image_url.url.", "messages");
    }
    if (is_oversized_data_url(*url)) {
        return std::nullopt;
    }
    Json mapped = JsonObject{};
    mapped["type"] = "input_image";
    mapped["image_url"] = *url;
    if (detail.has_value() && !detail->empty()) {
        mapped["detail"] = *detail;
    }
    return mapped;
}

std::optional<Json> map_chat_user_content_part(const Json& part) {
    if (part.is_string()) {
        return input_text_part(part.get_string());
    }
    if (!part.is_object()) {
        throw ChatValidationError("User message content parts must be objects.", "messages");
    }

    const auto& object = part.get_object();
    const auto part_type = infer_content_part_type(object);

    if (part_type == "text" || part_type == "input_text" || part_type == "output_text") {
        const auto text = json_string(object, "text");
        if (!text.has_value()) {
            throw ChatValidationError("Text content parts must include a string 'text'.", "messages");
        }
        return input_text_part(*text);
    }
    if (part_type == "image_url") {
        return map_chat_image_part(object);
    }
    if (part_type == "input_image") {
        return part;
    }
    if (part_type == "input_audio") {
        throw ChatValidationError("Audio input is not supported.", "messages");
    }
    if (part_type == "file") {
        return map_chat_file_part(object);
    }
    if (part_type == "input_file") {
        if (const auto file_id = json_string(object, "file_id"); file_id.has_value() && !file_id->empty()) {
            throw ChatValidationError("Invalid request payload", "messages");
        }
        return part;
    }
    throw ChatValidationError("Unsupported user content part type: " + part_type, "messages");
}

Json to_user_input_parts(const Json& content) {
    Json parts = JsonArray{};
    auto& array = parts.get_array();

    if (content.is_null()) {
        array.push_back(input_text_part(""));
        return parts;
    }
    if (content.is_array()) {
        for (const auto& part : content.get_array()) {
            const auto normalized = map_chat_user_content_part(part);
            if (normalized.has_value()) {
                array.push_back(*normalized);
            }
        }
    } else {
        const auto normalized = map_chat_user_content_part(content);
        if (normalized.has_value()) {
            array.push_back(*normalized);
        }
    }

    if (array.empty()) {
        array.push_back(input_text_part(""));
    }
    return parts;
}

Json to_input_parts(const Json& content) {
    Json parts = JsonArray{};
    auto& array = parts.get_array();

    auto append_text = [&array](const std::string& text) { array.push_back(input_text_part(text)); };

    if (content.is_string()) {
        append_text(content.get_string());
        return parts;
    }

    const auto parse_content_part = [&append_text, &array](const Json& part) {
        if (part.is_string()) {
            append_text(part.get_string());
            return;
        }
        if (!part.is_object()) {
            return;
        }
        const auto& object = part.get_object();
        const auto type = json_string(object, "type");
        if (type.has_value() && *type == "image_url") {
            const auto image_it = object.find("image_url");
            if (image_it != object.end() && image_it->second.is_object()) {
                const auto url = json_string(image_it->second.get_object(), "url");
                if (url.has_value() && !url->empty()) {
                    Json image = JsonObject{};
                    image["type"] = "input_image";
                    image["image_url"] = *url;
                    array.push_back(std::move(image));
                    return;
                }
            }
        }
        if (type.has_value() && (*type == "input_text" || *type == "input_image")) {
            array.push_back(part);
            return;
        }
        if (type.has_value() && *type == "file") {
            array.push_back(map_chat_file_part(object));
            return;
        }
        const auto text = json_string(object, "text");
        if (text.has_value()) {
            append_text(*text);
        }
    };

    if (content.is_array()) {
        for (const auto& part : content.get_array()) {
            parse_content_part(part);
        }
    } else if (content.is_object()) {
        parse_content_part(content);
    }

    if (array.empty()) {
        append_text("");
    }
    return parts;
}

std::string flatten_text_content(const Json& content) {
    if (content.is_string()) {
        return content.get_string();
    }
    if (content.is_object()) {
        const auto text = json_string(content.get_object(), "text");
        return text.value_or("");
    }
    if (!content.is_array()) {
        return "";
    }
    std::string joined;
    for (const auto& part : content.get_array()) {
        if (part.is_string()) {
            if (!joined.empty()) {
                joined.push_back('\n');
            }
            joined += part.get_string();
            continue;
        }
        if (part.is_object()) {
            const auto text = json_string(part.get_object(), "text");
            if (text.has_value()) {
                if (!joined.empty()) {
                    joined.push_back('\n');
                }
                joined += *text;
            }
        }
    }
    return joined;
}

struct ChatRequestTransform {
    std::string model;
    bool stream_requested = false;
    bool include_usage = false;
    std::string responses_body;
};

void normalize_chat_tools_field(JsonObject& object) {
    const auto tools_it = object.find("tools");
    if (tools_it == object.end()) {
        return;
    }
    if (!tools_it->second.is_array()) {
        throw ChatValidationError("Unsupported tool type");
    }

    Json normalized_tools = JsonArray{};
    for (const auto& tool : tools_it->second.get_array()) {
        if (!tool.is_object()) {
            continue;
        }
        const auto& tool_object = tool.get_object();
        auto tool_type = json_string(tool_object, "type").value_or("");
        if (tool_type.empty()) {
            tool_type = "function";
        }
        tool_type = openai::internal::normalize_tool_type_alias(tool_type);
        if (openai::internal::contains_string(openai::internal::kUnsupportedToolTypes, tool_type)) {
            throw ChatValidationError("Unsupported tool type");
        }

        const auto function_it = tool_object.find("function");
        if (function_it != tool_object.end() && function_it->second.is_object()) {
            const auto& function_object = function_it->second.get_object();
            const auto name = json_string(function_object, "name");
            if (!name.has_value() || name->empty()) {
                continue;
            }
            Json normalized = JsonObject{};
            normalized["type"] = tool_type;
            normalized["name"] = *name;
            const auto description = json_string(function_object, "description");
            if (description.has_value()) {
                normalized["description"] = *description;
            }
            const auto parameters_it = function_object.find("parameters");
            if (parameters_it != function_object.end()) {
                normalized["parameters"] = parameters_it->second;
            }
            normalized_tools.get_array().push_back(std::move(normalized));
            continue;
        }

        Json normalized = tool;
        auto& normalized_object = normalized.get_object();
        normalized_object["type"] = tool_type;
        if (tool_type == "web_search") {
            normalized_tools.get_array().push_back(std::move(normalized));
            continue;
        }
        const auto name = json_string(normalized_object, "name");
        if (name.has_value() && !name->empty()) {
            normalized_tools.get_array().push_back(std::move(normalized));
        }
    }
    tools_it->second = std::move(normalized_tools);
}

void normalize_chat_tool_choice_field(JsonObject& object) {
    const auto choice_it = object.find("tool_choice");
    if (choice_it == object.end() || !choice_it->second.is_object()) {
        return;
    }
    auto& choice_object = choice_it->second.get_object();
    auto type = json_string(choice_object, "type").value_or("");
    if (!type.empty()) {
        choice_object["type"] = openai::internal::normalize_tool_type_alias(type);
    }
    const auto function_it = choice_object.find("function");
    if (function_it == choice_object.end() || !function_it->second.is_object()) {
        return;
    }
    const auto name = json_string(function_it->second.get_object(), "name");
    if (!name.has_value() || name->empty()) {
        return;
    }
    Json normalized = JsonObject{};
    normalized["type"] = type.empty() ? "function" : openai::internal::normalize_tool_type_alias(type);
    normalized["name"] = *name;
    choice_it->second = std::move(normalized);
}

Json response_format_to_text_format(const Json& response_format) {
    auto fail = [](const std::string& message) -> ChatValidationError { return ChatValidationError(message, "response_format"); };

    if (response_format.is_string()) {
        const auto type = response_format.get_string();
        if (type == "json_object" || type == "text") {
            Json format = JsonObject{};
            format["type"] = type;
            return format;
        }
        if (type == "json_schema") {
            throw fail("'response_format' must include 'json_schema' when type is 'json_schema'.");
        }
        throw fail("Unsupported response_format.type: " + type);
    }

    if (!response_format.is_object()) {
        throw fail("'response_format' must be a string or object.");
    }
    const auto& format_object = response_format.get_object();
    const auto type = json_string(format_object, "type");
    if (!type.has_value() || type->empty()) {
        throw fail("Unsupported response_format.type: ");
    }

    if (*type == "json_object" || *type == "text") {
        Json format = JsonObject{};
        format["type"] = *type;
        return format;
    }
    if (*type != "json_schema") {
        throw fail("Unsupported response_format.type: " + *type);
    }

    const auto schema_it = format_object.find("json_schema");
    if (schema_it == format_object.end() || !schema_it->second.is_object()) {
        throw fail("'response_format.json_schema' is required when type is 'json_schema'.");
    }
    const auto& schema_object = schema_it->second.get_object();
    const auto name = json_string(schema_object, "name");
    if (name.has_value() && !name->empty() && !is_valid_json_schema_name(*name)) {
        throw fail("Invalid response_format.json_schema.name");
    }

    Json format = JsonObject{};
    format["type"] = "json_schema";
    if (name.has_value() && !name->empty()) {
        format["name"] = *name;
    }
    const auto schema_value_it = schema_object.find("schema");
    if (schema_value_it != schema_object.end()) {
        format["schema"] = schema_value_it->second;
    }
    const auto strict_it = schema_object.find("strict");
    if (strict_it != schema_object.end() && strict_it->second.is_boolean()) {
        format["strict"] = strict_it->second.get_boolean();
    }
    return format;
}

void apply_response_format_mapping(JsonObject& object) {
    const auto response_format_it = object.find("response_format");
    if (response_format_it == object.end()) {
        return;
    }

    Json text_controls = JsonObject{};
    const auto text_it = object.find("text");
    if (text_it != object.end()) {
        if (!text_it->second.is_object()) {
            throw ChatValidationError("'text' must be an object when using 'response_format'.", "text");
        }
        text_controls = text_it->second;
        if (text_controls.get_object().find("format") != text_controls.get_object().end()) {
            throw ChatValidationError("Provide either 'response_format' or 'text.format', not both.", "response_format");
        }
    }

    text_controls["format"] = response_format_to_text_format(response_format_it->second);
    object["text"] = std::move(text_controls);
    object.erase(response_format_it);
}

void map_reasoning_effort(JsonObject& object) {
    const auto effort_it = object.find("reasoning_effort");
    if (effort_it == object.end() || !effort_it->second.is_string()) {
        return;
    }
    Json reasoning = JsonObject{};
    const auto existing_reasoning_it = object.find("reasoning");
    if (existing_reasoning_it != object.end() && existing_reasoning_it->second.is_object()) {
        reasoning = existing_reasoning_it->second;
    }
    if (reasoning.get_object().find("effort") == reasoning.get_object().end()) {
        reasoning["effort"] = effort_it->second.get_string();
    }
    object["reasoning"] = std::move(reasoning);
    object.erase(effort_it);
}

ChatRequestTransform transform_chat_request(const std::string& raw_request_body) {
    Json payload;
    if (const auto ec = glz::read_json(payload, raw_request_body); ec || !payload.is_object()) {
        throw std::runtime_error("request payload is not valid JSON");
    }
    auto& object = payload.get_object();

    const auto model = json_string(object, "model");
    if (!model.has_value() || model->empty()) {
        throw std::runtime_error("request payload missing required field: model");
    }

    const auto messages_it = object.find("messages");
    if (messages_it == object.end() || !messages_it->second.is_array() || messages_it->second.get_array().empty()) {
        throw std::runtime_error("'messages' must be a non-empty list.");
    }

    Json input = JsonArray{};
    auto& input_items = input.get_array();
    std::string instructions;
    bool first_instruction = true;

    for (const auto& message : messages_it->second.get_array()) {
        if (!message.is_object()) {
            throw ChatValidationError("'messages' must contain objects.", "messages");
        }
        const auto& message_object = message.get_object();
        const auto role = json_string(message_object, "role");
        if (!role.has_value() || !is_supported_role(*role)) {
            throw ChatValidationError("unsupported message role", "messages");
        }

        const auto content_it = message_object.find("content");
        const Json content = content_it == message_object.end() ? Json::null_t{} : content_it->second;
        if (*role == "system" || *role == "developer") {
            ensure_text_only_content(content, *role);
            const auto text = flatten_text_content(content);
            if (!text.empty()) {
                if (!first_instruction) {
                    instructions += "\n";
                }
                instructions += text;
                first_instruction = false;
            }
            continue;
        }

        if (*role == "tool") {
            const auto call_id = optional_tool_call_id(message_object);
            if (!call_id.has_value() || call_id->empty()) {
                throw ChatValidationError("tool messages must include 'tool_call_id'.", "messages");
            }
            if (content_it == message_object.end() || content_it->second.is_null()) {
                throw ChatValidationError("tool message content is required.", "messages");
            }
            Json output_item = JsonObject{};
            output_item["type"] = "function_call_output";
            output_item["call_id"] = *call_id;
            output_item["output"] = normalize_tool_output_content(content_it->second);
            input_items.push_back(std::move(output_item));
            continue;
        }

        Json item = JsonObject{};
        item["role"] = *role;
        item["content"] = *role == "user" ? to_user_input_parts(content) : to_input_parts(content);
        input_items.push_back(std::move(item));

        if (*role == "assistant") {
            const auto tool_calls_it = message_object.find("tool_calls");
            if (tool_calls_it != message_object.end() && tool_calls_it->second.is_array()) {
                for (const auto& tool_call : tool_calls_it->second.get_array()) {
                    if (!tool_call.is_object()) {
                        throw ChatValidationError("tool_calls entries must be objects.", "messages");
                    }
                    const auto& tool_call_object = tool_call.get_object();
                    const auto call_id = json_string(tool_call_object, "id");
                    if (!call_id.has_value() || call_id->empty()) {
                        throw ChatValidationError("tool_calls[].id is required.", "messages");
                    }
                    const auto function_it = tool_call_object.find("function");
                    if (function_it == tool_call_object.end() || !function_it->second.is_object()) {
                        throw ChatValidationError("tool_calls[].function is required.", "messages");
                    }
                    const auto function_name = json_string(function_it->second.get_object(), "name");
                    if (!function_name.has_value() || function_name->empty()) {
                        throw ChatValidationError("tool_calls[].function.name is required.", "messages");
                    }
                    const auto function_arguments = json_string(function_it->second.get_object(), "arguments");
                    if (!function_arguments.has_value()) {
                        throw ChatValidationError("tool_calls[].function.arguments must be a string.", "messages");
                    }
                    Json call_item = JsonObject{};
                    call_item["type"] = "function_call";
                    call_item["call_id"] = *call_id;
                    call_item["name"] = *function_name;
                    call_item["arguments"] = *function_arguments;
                    input_items.push_back(std::move(call_item));
                }
            }
        }
    }

    if (input_items.empty()) {
        Json fallback = JsonObject{};
        fallback["role"] = "user";
        fallback["content"] = JsonArray{};
        fallback["content"].get_array().push_back(input_text_part(""));
        input_items.push_back(std::move(fallback));
    }

    const auto stream_requested = json_bool(object, "stream", false);
    bool include_usage = false;
    std::optional<bool> include_obfuscation;
    const auto stream_options_it = object.find("stream_options");
    if (stream_options_it != object.end() && stream_options_it->second.is_object()) {
        include_usage = json_bool(stream_options_it->second.get_object(), "include_usage", false);
        const auto include_obfuscation_it = stream_options_it->second.get_object().find("include_obfuscation");
        if (include_obfuscation_it != stream_options_it->second.get_object().end() &&
            include_obfuscation_it->second.is_boolean()) {
            include_obfuscation = include_obfuscation_it->second.get_boolean();
        }
    }

    object.erase("messages");
    object.erase("n");
    object.erase("max_tokens");
    object.erase("max_completion_tokens");
    object.erase("stream_options");
    map_reasoning_effort(object);
    apply_response_format_mapping(object);
    normalize_chat_tools_field(object);
    normalize_chat_tool_choice_field(object);
    if (include_obfuscation.has_value()) {
        Json options = JsonObject{};
        options["include_obfuscation"] = *include_obfuscation;
        object["stream_options"] = std::move(options);
    }
    object["input"] = std::move(input);
    object["instructions"] = instructions;
    object["stream"] = true;

    const auto serialized = serialize_json(payload);
    if (!serialized.has_value()) {
        throw std::runtime_error("failed to serialize chat request");
    }

    return {
        .model = *model,
        .stream_requested = stream_requested,
        .include_usage = include_usage,
        .responses_body = *serialized,
    };
}

std::string extract_text_from_output_item(const Json& item) {
    if (!item.is_object()) {
        return {};
    }
    const auto& object = item.get_object();
    const auto type = json_string(object, "type");
    if (type.has_value() && *type == "output_text") {
        const auto text = json_string(object, "text");
        return text.value_or("");
    }
    const auto content_it = object.find("content");
    if (content_it != object.end() && content_it->second.is_array()) {
        std::string text;
        for (const auto& part : content_it->second.get_array()) {
            if (!part.is_object()) {
                continue;
            }
            const auto part_type = json_string(part.get_object(), "type");
            if (part_type.has_value() && (*part_type == "output_text" || *part_type == "text")) {
                const auto value = json_string(part.get_object(), "text");
                if (value.has_value()) {
                    text += *value;
                }
            }
        }
        return text;
    }
    return {};
}

Json chat_usage_from_response_usage(const Json& usage) {
    Json mapped = JsonObject{};
    if (!usage.is_object()) {
        return mapped;
    }
    const auto& usage_object = usage.get_object();
    const auto in_it = usage_object.find("input_tokens");
    const auto out_it = usage_object.find("output_tokens");
    const auto total_it = usage_object.find("total_tokens");
    if (in_it != usage_object.end()) {
        mapped["prompt_tokens"] = in_it->second;
    }
    if (out_it != usage_object.end()) {
        mapped["completion_tokens"] = out_it->second;
    }
    if (total_it != usage_object.end()) {
        mapped["total_tokens"] = total_it->second;
    }
    return mapped;
}

std::string finish_reason_from_incomplete(const Json& response);

ProxyJsonResult responses_json_to_chat_completion(const ProxyJsonResult& upstream, const std::string& model) {
    if (upstream.status >= 400) {
        return upstream;
    }

    Json payload;
    if (const auto ec = glz::read_json(payload, upstream.body); ec || !payload.is_object()) {
        return {
            .status = 502,
            .body = openai::build_error_envelope("upstream_error", "Invalid upstream response", "server_error"),
            .headers = upstream.headers,
        };
    }
    const auto& object = payload.get_object();
    if (object.find("error") != object.end()) {
        return upstream;
    }

    std::string id = "chatcmpl_temp";
    if (const auto value = json_string(object, "id"); value.has_value() && !value->empty()) {
        id = *value;
    }
    std::string resolved_model = model;
    if (const auto value = json_string(object, "model"); value.has_value() && !value->empty()) {
        resolved_model = *value;
    }

    std::string content;
    Json tool_calls = JsonArray{};
    const auto output_it = object.find("output");
    if (output_it != object.end() && output_it->second.is_array()) {
        for (const auto& item : output_it->second.get_array()) {
            content += extract_text_from_output_item(item);
            if (!item.is_object()) {
                continue;
            }

            const auto& item_object = item.get_object();
            const auto item_type = json_string(item_object, "type");
            if (!item_type.has_value() || (*item_type != "function_call" && *item_type != "tool_call")) {
                continue;
            }

            Json call = JsonObject{};
            auto call_id = json_string(item_object, "call_id");
            if (!call_id.has_value()) {
                call_id = json_string(item_object, "id");
            }
            if (!call_id.has_value()) {
                call_id = json_string(item_object, "tool_call_id");
            }
            if (call_id.has_value()) {
                call["id"] = *call_id;
            }
            call["type"] = "function";

            Json function = JsonObject{};
            const auto function_name = json_string(item_object, "name");
            if (function_name.has_value()) {
                function["name"] = *function_name;
            }
            const auto arguments = json_string(item_object, "arguments");
            if (arguments.has_value()) {
                function["arguments"] = *arguments;
            }
            if (!function.get_object().empty()) {
                call["function"] = std::move(function);
            }
            if (!call.get_object().empty()) {
                tool_calls.get_array().push_back(std::move(call));
            }
        }
    }
    const bool has_tool_calls = !tool_calls.get_array().empty();

    Json message = JsonObject{};
    message["role"] = "assistant";
    if (!content.empty() || !has_tool_calls) {
        message["content"] = content;
    } else {
        message["content"] = Json::null_t{};
    }
    if (has_tool_calls) {
        message["tool_calls"] = std::move(tool_calls);
    }

    Json choice = JsonObject{};
    choice["index"] = 0;
    choice["message"] = std::move(message);
    if (has_tool_calls) {
        choice["finish_reason"] = "tool_calls";
    } else {
        const auto status = json_string(object, "status");
        if (status.has_value() && *status == "incomplete") {
            choice["finish_reason"] = finish_reason_from_incomplete(payload);
        } else {
            choice["finish_reason"] = "stop";
        }
    }

    Json choices = JsonArray{};
    choices.get_array().push_back(std::move(choice));

    Json chat = JsonObject{};
    chat["id"] = id;
    chat["object"] = "chat.completion";
    chat["created"] = static_cast<std::int64_t>(std::time(nullptr));
    chat["model"] = resolved_model;
    chat["choices"] = std::move(choices);

    const auto usage_it = object.find("usage");
    if (usage_it != object.end()) {
        const auto mapped_usage = chat_usage_from_response_usage(usage_it->second);
        if (mapped_usage.is_object() && !mapped_usage.get_object().empty()) {
            chat["usage"] = mapped_usage;
        }
    }

    const auto serialized = serialize_json(chat);
    if (!serialized.has_value()) {
        return {
            .status = 500,
            .body = openai::build_error_envelope("server_error", "Failed to serialize chat completion", "server_error"),
            .headers = upstream.headers,
        };
    }
    return {
        .status = 200,
        .body = *serialized,
        .headers = upstream.headers,
    };
}

std::optional<Json> parse_event_json(const std::string& event) {
    Json payload;
    if (const auto ec = glz::read_json(payload, event); ec || !payload.is_object()) {
        return std::nullopt;
    }
    return payload;
}

struct ToolCallDelta {
    std::size_t index = 0;
    std::optional<std::string> call_id;
    std::optional<std::string> name;
    std::optional<std::string> arguments;
    std::optional<std::string> tool_type;
};

class ToolCallIndex {
public:
    [[nodiscard]] std::size_t index_for(
        const std::optional<std::string>& call_id,
        const std::optional<std::string>& name
    ) {
        std::string key;
        if (call_id.has_value() && !call_id->empty()) {
            key = "id:" + *call_id;
        } else if (name.has_value() && !name->empty()) {
            key = "name:" + *name;
        } else {
            return 0;
        }

        const auto existing = indexes_.find(key);
        if (existing != indexes_.end()) {
            return existing->second;
        }
        const auto next = next_index_++;
        indexes_.emplace(std::move(key), next);
        return next;
    }

private:
    std::unordered_map<std::string, std::size_t> indexes_;
    std::size_t next_index_ = 0;
};

struct ToolCallState {
    std::size_t index = 0;
    std::optional<std::string> call_id;
    std::optional<std::string> name;
    std::string arguments;
    std::string tool_type = "function";
};

std::optional<std::string> first_non_empty(std::initializer_list<std::optional<std::string>> values) {
    for (const auto& value : values) {
        if (value.has_value() && !value->empty()) {
            return value;
        }
    }
    return std::nullopt;
}

std::optional<std::string> non_empty_json_string(const Json& value) {
    if (!value.is_string()) {
        return std::nullopt;
    }
    const auto text = value.get_string();
    if (text.empty()) {
        return std::nullopt;
    }
    return text;
}

bool contains_tool_fields(const JsonObject& object) {
    return object.find("call_id") != object.end() || object.find("tool_call_id") != object.end() ||
           object.find("arguments") != object.end() || object.find("function") != object.end() ||
           object.find("name") != object.end();
}

bool is_tool_call_event(const JsonObject& object) {
    const auto type = json_string(object, "type");
    if (type.has_value() && (type->find("tool_call") != std::string::npos || type->find("function_call") != std::string::npos)) {
        return true;
    }

    const auto item_it = object.find("item");
    if (item_it != object.end() && item_it->second.is_object()) {
        const auto& item = item_it->second.get_object();
        const auto item_type = json_string(item, "type");
        if (item_type.has_value() &&
            (item_type->find("tool") != std::string::npos || item_type->find("function") != std::string::npos)) {
            return true;
        }
        if (contains_tool_fields(item)) {
            return true;
        }
    }

    if (object.find("call_id") != object.end() || object.find("tool_call_id") != object.end()) {
        return true;
    }
    return object.find("arguments") != object.end() &&
           (object.find("name") != object.end() || object.find("function") != object.end());
}

const JsonObject* select_tool_call_candidate(const JsonObject& object) {
    const auto item_it = object.find("item");
    if (item_it != object.end() && item_it->second.is_object()) {
        const auto& item = item_it->second.get_object();
        const auto item_type = json_string(item, "type");
        if ((item_type.has_value() &&
             (item_type->find("tool") != std::string::npos || item_type->find("function") != std::string::npos)) ||
            contains_tool_fields(item)) {
            return &item;
        }
    }
    return &object;
}

std::optional<ToolCallDelta> extract_tool_call_delta(const Json& payload, ToolCallIndex& indexer) {
    if (!payload.is_object()) {
        return std::nullopt;
    }
    const auto& object = payload.get_object();
    if (!is_tool_call_event(object)) {
        return std::nullopt;
    }

    const JsonObject* candidate = select_tool_call_candidate(object);
    if (candidate == nullptr) {
        return std::nullopt;
    }

    const auto delta_it = candidate->find("delta");
    const JsonObject* delta_object = nullptr;
    std::optional<std::string> delta_text;
    if (delta_it != candidate->end()) {
        if (delta_it->second.is_object()) {
            delta_object = &delta_it->second.get_object();
        } else {
            delta_text = non_empty_json_string(delta_it->second);
        }
    }

    auto call_id = first_non_empty({
        json_string(*candidate, "call_id"),
        json_string(*candidate, "tool_call_id"),
        json_string(*candidate, "id"),
    });
    if (!call_id.has_value() && delta_object != nullptr) {
        call_id = first_non_empty({
            json_string(*delta_object, "id"),
            json_string(*delta_object, "call_id"),
            json_string(*delta_object, "tool_call_id"),
        });
    }

    auto name = first_non_empty({
        json_string(*candidate, "name"),
        json_string(*candidate, "tool_name"),
    });
    if (!name.has_value() && delta_object != nullptr) {
        name = json_string(*delta_object, "name");
    }
    if (!name.has_value()) {
        const auto function_it = candidate->find("function");
        if (function_it != candidate->end() && function_it->second.is_object()) {
            name = json_string(function_it->second.get_object(), "name");
        }
    }
    if (!name.has_value() && delta_object != nullptr) {
        const auto function_it = delta_object->find("function");
        if (function_it != delta_object->end() && function_it->second.is_object()) {
            name = json_string(function_it->second.get_object(), "name");
        }
    }

    std::optional<std::string> arguments = json_string(*candidate, "arguments");
    if (!arguments.has_value()) {
        arguments = delta_text;
    }
    if (!arguments.has_value() && delta_object != nullptr) {
        arguments = json_string(*delta_object, "arguments");
    }
    if (!arguments.has_value() && delta_object != nullptr) {
        const auto function_it = delta_object->find("function");
        if (function_it != delta_object->end() && function_it->second.is_object()) {
            arguments = json_string(function_it->second.get_object(), "arguments");
        }
    }

    auto tool_type = first_non_empty({
        json_string(*candidate, "tool_type"),
        json_string(*candidate, "type"),
    });
    if (tool_type.has_value() && core::text::starts_with(*tool_type, "response.")) {
        tool_type = std::nullopt;
    }
    if (tool_type.has_value() && (*tool_type == "tool_call" || *tool_type == "function_call")) {
        tool_type = std::string("function");
    }

    if (!call_id.has_value() && !name.has_value() && !arguments.has_value()) {
        return std::nullopt;
    }

    return ToolCallDelta{
        .index = indexer.index_for(call_id, name),
        .call_id = call_id,
        .name = name,
        .arguments = arguments,
        .tool_type = tool_type,
    };
}

void merge_tool_call_delta(std::vector<ToolCallState>& tool_calls, const ToolCallDelta& delta) {
    while (tool_calls.size() <= delta.index) {
        tool_calls.push_back(ToolCallState{.index = tool_calls.size()});
    }

    auto& state = tool_calls[delta.index];
    if (delta.call_id.has_value() && !delta.call_id->empty()) {
        state.call_id = delta.call_id;
    }
    if (delta.name.has_value() && !delta.name->empty()) {
        state.name = delta.name;
    }
    if (delta.arguments.has_value() && !delta.arguments->empty()) {
        state.arguments += *delta.arguments;
    }
    if (delta.tool_type.has_value() && !delta.tool_type->empty()) {
        state.tool_type = *delta.tool_type;
    }
}

Json tool_call_delta_to_chunk(const ToolCallDelta& delta) {
    Json call = JsonObject{};
    call["index"] = static_cast<std::int64_t>(delta.index);
    if (delta.call_id.has_value() && !delta.call_id->empty()) {
        call["id"] = *delta.call_id;
    }
    call["type"] = delta.tool_type.value_or("function");
    if (delta.name.has_value() || delta.arguments.has_value()) {
        Json function = JsonObject{};
        if (delta.name.has_value() && !delta.name->empty()) {
            function["name"] = *delta.name;
        }
        if (delta.arguments.has_value()) {
            function["arguments"] = *delta.arguments;
        }
        call["function"] = std::move(function);
    }
    return call;
}

Json compact_tool_calls_for_message(const std::vector<ToolCallState>& tool_calls) {
    Json result = JsonArray{};
    for (const auto& call_state : tool_calls) {
        if (!call_state.call_id.has_value() && !call_state.name.has_value() && call_state.arguments.empty()) {
            continue;
        }

        Json call = JsonObject{};
        if (call_state.call_id.has_value() && !call_state.call_id->empty()) {
            call["id"] = *call_state.call_id;
        }
        call["type"] = call_state.tool_type.empty() ? "function" : call_state.tool_type;
        if (call_state.name.has_value() || !call_state.arguments.empty()) {
            Json function = JsonObject{};
            if (call_state.name.has_value() && !call_state.name->empty()) {
                function["name"] = *call_state.name;
            }
            if (!call_state.arguments.empty()) {
                function["arguments"] = call_state.arguments;
            }
            call["function"] = std::move(function);
        }
        result.get_array().push_back(std::move(call));
    }
    return result;
}

std::string finish_reason_from_incomplete(const Json& response) {
    if (!response.is_object()) {
        return "stop";
    }
    const auto details_it = response.get_object().find("incomplete_details");
    if (details_it == response.get_object().end() || !details_it->second.is_object()) {
        return "stop";
    }
    const auto reason = json_string(details_it->second.get_object(), "reason");
    if (!reason.has_value()) {
        return "stop";
    }
    if (*reason == "max_output_tokens" || *reason == "max_tokens") {
        return "length";
    }
    if (*reason == "content_filter") {
        return "content_filter";
    }
    return "stop";
}

std::optional<std::string> build_chat_chunk_json(
    const std::string& id,
    const std::string& model,
    const Json& delta,
    const std::optional<std::string>& finish_reason,
    const bool include_usage = false
) {
    Json choice = JsonObject{};
    choice["index"] = 0;
    choice["delta"] = delta;
    if (finish_reason.has_value()) {
        choice["finish_reason"] = *finish_reason;
    }

    Json choices = JsonArray{};
    choices.get_array().push_back(std::move(choice));

    Json chunk = JsonObject{};
    chunk["id"] = id;
    chunk["object"] = "chat.completion.chunk";
    chunk["created"] = static_cast<std::int64_t>(std::time(nullptr));
    chunk["model"] = model;
    chunk["choices"] = std::move(choices);
    if (include_usage) {
        chunk["usage"] = Json::null_t{};
    }
    return serialize_json(chunk);
}

std::vector<std::string> responses_events_to_chat_events(
    const std::vector<std::string>& events,
    const std::string& model,
    const bool include_usage
) {
    std::vector<std::string> out;
    std::string chunk_id = "chatcmpl_temp";
    bool sent_role = false;
    bool saw_tool_call = false;
    ToolCallIndex tool_index;

    for (const auto& event : events) {
        const auto payload = parse_event_json(event);
        if (!payload.has_value()) {
            continue;
        }
        const auto& object = payload->get_object();
        const auto type = json_string(object, "type");
        if (!type.has_value()) {
            continue;
        }

        if (*type == "response.output_text.delta") {
            Json delta = JsonObject{};
            const auto delta_text = json_string(object, "delta");
            if (!sent_role) {
                delta["role"] = "assistant";
            }
            delta["content"] = delta_text.value_or("");
            const auto chunk = build_chat_chunk_json(chunk_id, model, delta, std::nullopt, include_usage);
            if (chunk.has_value()) {
                out.push_back(*chunk);
            }
            sent_role = true;
            continue;
        }

        if (const auto tool_delta = extract_tool_call_delta(*payload, tool_index); tool_delta.has_value()) {
            Json delta = JsonObject{};
            if (!sent_role) {
                delta["role"] = "assistant";
            }
            Json calls = JsonArray{};
            calls.get_array().push_back(tool_call_delta_to_chunk(*tool_delta));
            delta["tool_calls"] = std::move(calls);
            const auto chunk = build_chat_chunk_json(chunk_id, model, delta, std::nullopt, include_usage);
            if (chunk.has_value()) {
                out.push_back(*chunk);
            }
            sent_role = true;
            saw_tool_call = true;
        }

        if (*type == "response.failed" || *type == "error") {
            Json envelope = JsonObject{};
            if (*type == "response.failed") {
                const auto response_it = object.find("response");
                if (response_it != object.end() && response_it->second.is_object()) {
                    const auto error_it = response_it->second.get_object().find("error");
                    if (error_it != response_it->second.get_object().end()) {
                        envelope["error"] = error_it->second;
                    }
                }
            } else {
                const auto error_it = object.find("error");
                if (error_it != object.end()) {
                    envelope["error"] = error_it->second;
                }
            }
            if (envelope.get_object().find("error") == envelope.get_object().end()) {
                envelope["error"] = JsonObject{};
                envelope["error"].get_object()["message"] = "Upstream error";
                envelope["error"].get_object()["type"] = "server_error";
                envelope["error"].get_object()["code"] = "upstream_error";
            }
            const auto serialized = serialize_json(envelope);
            if (serialized.has_value()) {
                out.push_back(*serialized);
            }
            out.push_back("[DONE]");
            return out;
        }

        if (*type == "response.completed" || *type == "response.incomplete") {
            const auto response_it = object.find("response");
            Json usage = JsonObject{};
            if (response_it != object.end() && response_it->second.is_object()) {
                const auto response_id = json_string(response_it->second.get_object(), "id");
                if (response_id.has_value() && !response_id->empty()) {
                    chunk_id = *response_id;
                }
                const auto usage_it = response_it->second.get_object().find("usage");
                if (usage_it != response_it->second.get_object().end()) {
                    usage = usage_it->second;
                }
            }

            Json final_delta = JsonObject{};
            const auto finish_reason = saw_tool_call
                                           ? std::string("tool_calls")
                                           : ((*type == "response.incomplete" && response_it != object.end())
                                                  ? finish_reason_from_incomplete(response_it->second)
                                                  : std::string("stop"));
            const auto final_chunk = build_chat_chunk_json(chunk_id, model, final_delta, finish_reason, include_usage);
            if (final_chunk.has_value()) {
                out.push_back(*final_chunk);
            }

            if (include_usage) {
                Json usage_chunk = JsonObject{};
                usage_chunk["id"] = chunk_id;
                usage_chunk["object"] = "chat.completion.chunk";
                usage_chunk["created"] = static_cast<std::int64_t>(std::time(nullptr));
                usage_chunk["model"] = model;
                usage_chunk["choices"] = JsonArray{};
                usage_chunk["usage"] = chat_usage_from_response_usage(usage);
                if (const auto serialized = serialize_json(usage_chunk); serialized.has_value()) {
                    out.push_back(*serialized);
                }
            }

            out.push_back("[DONE]");
            return out;
        }
    }

    out.push_back(openai::build_error_envelope("upstream_error", "Upstream stream was incomplete", "server_error"));
    out.push_back("[DONE]");
    return out;
}

} // namespace

bool chat_completions_stream_requested(const std::string& raw_request_body) {
    try {
        return transform_chat_request(raw_request_body).stream_requested;
    } catch (...) {
        return false;
    }
}

ProxyJsonResult collect_chat_completions_json(
    const std::string& raw_request_body,
    const openai::HeaderMap& inbound_headers
) {
    ChatRequestTransform transformed;
    try {
        transformed = transform_chat_request(raw_request_body);
    } catch (const ChatValidationError& ex) {
        return {
            .status = 400,
            .body = openai::build_error_envelope("invalid_request_error", ex.what(), "invalid_request_error", ex.param()),
        };
    } catch (const std::exception& ex) {
        return {
            .status = 400,
            .body = openai::build_error_envelope("invalid_request_error", ex.what()),
        };
    }

    const auto upstream = collect_responses_json("/v1/responses", transformed.responses_body, inbound_headers);
    return responses_json_to_chat_completion(upstream, transformed.model);
}

ProxySseResult stream_chat_completions_sse(
    const std::string& raw_request_body,
    const openai::HeaderMap& inbound_headers
) {
    ChatRequestTransform transformed;
    try {
        transformed = transform_chat_request(raw_request_body);
    } catch (const ChatValidationError& ex) {
        return {
            .status = 400,
            .events = {
                openai::build_error_envelope("invalid_request_error", ex.what(), "invalid_request_error", ex.param()),
            },
        };
    } catch (const std::exception& ex) {
        return {
            .status = 400,
            .events = {openai::build_error_envelope("invalid_request_error", ex.what())},
        };
    }

    const auto upstream = stream_responses_sse("/v1/responses", transformed.responses_body, inbound_headers);
    if (upstream.status >= 400) {
        return upstream;
    }
    return {
        .status = 200,
        .events = responses_events_to_chat_events(upstream.events, transformed.model, transformed.include_usage),
        .headers = upstream.headers,
    };
}

} // namespace tightrope::proxy
