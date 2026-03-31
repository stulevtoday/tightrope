#include "session/http_bridge.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <glaze/glaze.hpp>

#include "session_bridge.h"
#include "text/ascii.h"

namespace tightrope::proxy::session {

namespace {

using Json = glz::generic;
using JsonObject = Json::object_t;

constexpr std::int64_t kBridgeTtlMs = 30 * 60 * 1000;
constexpr std::array<std::string_view, 3> kSessionHeaders = {
    "session_id",
    "x-codex-session-id",
    "x-codex-conversation-id",
};

SessionBridge& http_bridge() {
    static SessionBridge bridge(kBridgeTtlMs);
    return bridge;
}

std::mutex& http_bridge_mutex() {
    static auto* value = new std::mutex();
    return *value;
}

std::int64_t now_ms() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::string find_header_case_insensitive(const openai::HeaderMap& headers, const std::string_view key) {
    for (const auto& [name, value] : headers) {
        if (core::text::equals_case_insensitive(name, key)) {
            return std::string(core::text::trim_ascii(value));
        }
    }
    return {};
}

std::optional<std::string> continuity_key_from_headers(const openai::HeaderMap& headers) {
    auto turn_state = find_header_case_insensitive(headers, "x-codex-turn-state");
    if (!turn_state.empty()) {
        return turn_state;
    }

    for (const auto header : kSessionHeaders) {
        auto session_value = find_header_case_insensitive(headers, header);
        if (!session_value.empty()) {
            return session_value;
        }
    }
    return std::nullopt;
}

std::optional<std::string> serialize_json(const JsonObject& object) {
    const auto serialized = glz::write_json(object);
    if (!serialized) {
        return std::nullopt;
    }
    return serialized.value_or("{}");
}

std::optional<std::string> parse_previous_response_id(const std::string& raw_request_body) {
    Json payload;
    if (const auto ec = glz::read_json(payload, raw_request_body); ec || !payload.is_object()) {
        return std::nullopt;
    }

    const auto& object = payload.get_object();
    const auto previous_it = object.find("previous_response_id");
    if (previous_it == object.end() || !previous_it->second.is_string()) {
        return std::nullopt;
    }

    const auto value = std::string(core::text::trim_ascii(previous_it->second.get_string()));
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::string> inject_previous_response_id(
    const std::string& raw_request_body,
    const std::string& previous_response_id
) {
    Json payload;
    if (const auto ec = glz::read_json(payload, raw_request_body); ec || !payload.is_object()) {
        return std::nullopt;
    }
    auto object = payload.get_object();
    object["previous_response_id"] = previous_response_id;
    return serialize_json(object);
}

std::optional<std::string> strip_previous_response_id_impl(const std::string& raw_request_body) {
    Json payload;
    if (const auto ec = glz::read_json(payload, raw_request_body); ec || !payload.is_object()) {
        return std::nullopt;
    }
    auto object = payload.get_object();
    object.erase("previous_response_id");
    return serialize_json(object);
}

std::optional<std::string> response_id_from_json_body(const std::string& response_body) {
    Json payload;
    if (const auto ec = glz::read_json(payload, response_body); ec || !payload.is_object()) {
        return std::nullopt;
    }

    const auto& object = payload.get_object();
    const auto id_it = object.find("id");
    if (id_it != object.end() && id_it->second.is_string()) {
        const auto id = std::string(core::text::trim_ascii(id_it->second.get_string()));
        if (!id.empty()) {
            return id;
        }
    }

    const auto response_it = object.find("response");
    if (response_it != object.end() && response_it->second.is_object()) {
        const auto nested_id_it = response_it->second.get_object().find("id");
        if (nested_id_it != response_it->second.get_object().end() && nested_id_it->second.is_string()) {
            const auto id = std::string(core::text::trim_ascii(nested_id_it->second.get_string()));
            if (!id.empty()) {
                return id;
            }
        }
    }

    return std::nullopt;
}

std::optional<std::string> response_id_from_events(const std::vector<std::string>& events) {
    std::optional<std::string> fallback;
    for (const auto& event : events) {
        Json payload;
        if (const auto ec = glz::read_json(payload, event); ec || !payload.is_object()) {
            continue;
        }

        const auto& object = payload.get_object();
        const auto type_it = object.find("type");
        if (type_it == object.end() || !type_it->second.is_string()) {
            continue;
        }
        const auto type = type_it->second.get_string();
        if (type != "response.created" && type != "response.completed" && type != "response.incomplete") {
            continue;
        }

        const auto response_it = object.find("response");
        if (response_it == object.end() || !response_it->second.is_object()) {
            continue;
        }
        const auto id_it = response_it->second.get_object().find("id");
        if (id_it == response_it->second.get_object().end() || !id_it->second.is_string()) {
            continue;
        }

        const auto id = std::string(core::text::trim_ascii(id_it->second.get_string()));
        if (id.empty()) {
            continue;
        }

        if (type == "response.completed" || type == "response.incomplete") {
            return id;
        }
        fallback = id;
    }

    return fallback;
}

std::string generate_turn_state() {
    static thread_local boost::uuids::random_generator generator;
    return "http_turn_" + boost::uuids::to_string(generator());
}

void remember_response_id(const openai::HeaderMap& headers, const std::optional<std::string>& response_id) {
    if (!response_id.has_value() || response_id->empty()) {
        return;
    }
    const auto key = continuity_key_from_headers(headers);
    if (!key.has_value() || key->empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(http_bridge_mutex());
    auto& bridge = http_bridge();
    const auto now = now_ms();
    bridge.upsert(*key, *response_id, now);
    (void)bridge.purge_stale(now);
}

} // namespace

std::string ensure_turn_state_header(openai::HeaderMap& headers) {
    auto turn_state = find_header_case_insensitive(headers, "x-codex-turn-state");
    if (turn_state.empty()) {
        const auto continuity_key = continuity_key_from_headers(headers);
        turn_state = continuity_key.has_value() ? *continuity_key : generate_turn_state();
    }
    headers["x-codex-turn-state"] = turn_state;
    return turn_state;
}

std::string inject_previous_response_id_from_bridge(
    const std::string& raw_request_body,
    const openai::HeaderMap& headers
) {
    if (parse_previous_response_id(raw_request_body).has_value()) {
        return raw_request_body;
    }

    const auto key = continuity_key_from_headers(headers);
    if (!key.has_value() || key->empty()) {
        return raw_request_body;
    }

    std::optional<std::string> bridged_previous_response_id;
    {
        std::lock_guard<std::mutex> lock(http_bridge_mutex());
        auto& bridge = http_bridge();
        const auto now = now_ms();
        if (const auto* existing = bridge.find(*key, now); existing != nullptr) {
            bridged_previous_response_id = existing->upstream_session_id;
        }
        (void)bridge.purge_stale(now);
    }
    if (!bridged_previous_response_id.has_value() || bridged_previous_response_id->empty()) {
        return raw_request_body;
    }

    const auto injected = inject_previous_response_id(raw_request_body, *bridged_previous_response_id);
    if (!injected.has_value()) {
        return raw_request_body;
    }
    return *injected;
}

std::optional<std::string> strip_previous_response_id(const std::string& raw_request_body) {
    return strip_previous_response_id_impl(raw_request_body);
}

void remember_response_id_from_json(const openai::HeaderMap& headers, const std::string& response_body) {
    remember_response_id(headers, response_id_from_json_body(response_body));
}

void remember_response_id_from_events(const openai::HeaderMap& headers, const std::vector<std::string>& events) {
    remember_response_id(headers, response_id_from_events(events));
}

} // namespace tightrope::proxy::session
