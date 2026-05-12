#include "session/http_bridge.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <glaze/glaze.hpp>

#include "config_loader.h"
#include "connection/sqlite_pool.h"
#include "repositories/session_repo.h"
#include "session_bridge.h"
#include "text/ascii.h"
#include "time/clock.h"

namespace tightrope::proxy::session {

namespace {

using Json = glz::generic;
using JsonObject = Json::object_t;
using JsonArray = Json::array_t;

constexpr std::int64_t kBridgeTtlMs = 30 * 60 * 1000;
constexpr std::int64_t kPersistenceCleanupIntervalMs = 60 * 1000;
constexpr std::size_t kMaxRecoveryInputItems = 16;
constexpr std::size_t kMaxRecoveryInputBytes = 96 * 1024;
constexpr std::size_t kMaxRecoveryTextBytes = 16 * 1024;
struct BridgePersistenceState {
    std::string db_path;
    std::unique_ptr<db::SqlitePool> pool;
    bool schema_ready = false;
    std::int64_t last_cleanup_ms = 0;
};

SessionBridge& http_bridge() {
    static SessionBridge bridge(kBridgeTtlMs);
    return bridge;
}

std::mutex& http_bridge_mutex() {
    static auto* value = new std::mutex();
    return *value;
}

BridgePersistenceState& bridge_persistence_state() {
    static BridgePersistenceState state;
    return state;
}

core::time::Clock& runtime_clock() {
    static core::time::SystemClock clock;
    return clock;
}

std::int64_t now_ms() {
    return runtime_clock().unix_ms_now();
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
    const auto codex_session_id = find_header_case_insensitive(headers, "x-codex-session-id");
    const auto client_session_id = find_header_case_insensitive(headers, "session_id");
    const auto turn_state = find_header_case_insensitive(headers, "x-codex-turn-state");

    // Codex turn-state is the narrowest continuity token. Session identifiers remain a legacy
    // bridge fallback for clients that do not replay x-codex-turn-state.
    if (!turn_state.empty()) {
        return turn_state;
    }
    if (!codex_session_id.empty() && !client_session_id.empty()) {
        if (codex_session_id == client_session_id) {
            return codex_session_id;
        }
        return "codex:" + codex_session_id + "|client:" + client_session_id;
    }
    if (!codex_session_id.empty()) {
        return codex_session_id;
    }
    if (!client_session_id.empty()) {
        return client_session_id;
    }
    return std::nullopt;
}

std::string api_key_scope_from_headers(const openai::HeaderMap& headers) {
    return find_header_case_insensitive(headers, "x-tightrope-api-key-id");
}

std::string account_id_from_headers(const openai::HeaderMap& headers) {
    return find_header_case_insensitive(headers, "chatgpt-account-id");
}

std::string scoped_bridge_key(const std::string_view continuity_key, const std::string_view api_key_scope) {
    if (api_key_scope.empty()) {
        return std::string(continuity_key);
    }
    return "api_key_scope:" + std::string(api_key_scope) + "|" + std::string(continuity_key);
}

sqlite3* ensure_persistence_db(BridgePersistenceState& state) {
    const auto config = config::load_config();
    const auto desired_path = config.db_path.empty() ? std::string("store.db") : config.db_path;

    if (!state.pool || state.db_path != desired_path) {
        if (state.pool) {
            state.pool->close();
        }
        state.pool = std::make_unique<db::SqlitePool>(desired_path);
        state.db_path = desired_path;
        state.schema_ready = false;
        state.last_cleanup_ms = 0;
    }

    if (!state.pool->open()) {
        return nullptr;
    }

    sqlite3* db = state.pool->connection();
    if (db == nullptr) {
        return nullptr;
    }

    if (!state.schema_ready) {
        if (!db::ensure_proxy_response_continuity_schema(db)) {
            return nullptr;
        }
        state.schema_ready = true;
        state.last_cleanup_ms = 0;
    }

    return db;
}

void maybe_purge_expired_persistence_rows(BridgePersistenceState& state, sqlite3* db, const std::int64_t now) {
    if (db == nullptr) {
        return;
    }
    if (state.last_cleanup_ms > 0 && (now - state.last_cleanup_ms) < kPersistenceCleanupIntervalMs) {
        return;
    }
    (void)db::purge_expired_proxy_response_continuity(db, now);
    state.last_cleanup_ms = now;
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

bool is_function_call_output_item(const Json& value) {
    if (!value.is_object()) {
        return false;
    }
    const auto& object = value.get_object();

    const auto type_it = object.find("type");
    if (type_it != object.end() && type_it->second.is_string() && type_it->second.get_string() == "function_call_output") {
        return true;
    }

    const bool has_call_id = object.find("call_id") != object.end() || object.find("tool_call_id") != object.end();
    if (!has_call_id) {
        return false;
    }
    return object.find("output") != object.end() || object.find("content") != object.end();
}

bool object_contains_function_call_output(const JsonObject& object) {
    const auto input_it = object.find("input");
    if (input_it != object.end() && input_it->second.is_array()) {
        const auto& input = input_it->second.get_array();
        for (const auto& item : input) {
            if (is_function_call_output_item(item)) {
                return true;
            }
        }
    }

    const auto response_it = object.find("response");
    if (response_it != object.end() && response_it->second.is_object()) {
        return object_contains_function_call_output(response_it->second.get_object());
    }
    return false;
}

std::optional<std::string> json_string_field(const JsonObject& object, const std::string_view key) {
    const auto it = object.find(std::string(key));
    if (it == object.end() || !it->second.is_string()) {
        return std::nullopt;
    }
    return std::string(it->second.get_string());
}

bool object_has_nonempty_tools(const JsonObject& object) {
    const auto tools_it = object.find("tools");
    return tools_it != object.end() && tools_it->second.is_array() && !tools_it->second.get_array().empty();
}

std::string bounded_text(std::string value) {
    if (value.size() <= kMaxRecoveryTextBytes) {
        return value;
    }
    value.resize(kMaxRecoveryTextBytes);
    return value;
}

Json text_part(const std::string_view type, std::string text) {
    Json part = JsonObject{};
    part["type"] = std::string(type);
    part["text"] = bounded_text(std::move(text));
    return part;
}

Json user_text_item(std::string text) {
    Json content = JsonArray{};
    content.get_array().push_back(text_part("input_text", std::move(text)));

    Json item = JsonObject{};
    item["role"] = "user";
    item["content"] = std::move(content);
    return item;
}

std::optional<JsonArray> request_input_items_for_recovery(const JsonObject& object) {
    if (object_has_nonempty_tools(object) || object_contains_function_call_output(object)) {
        return std::nullopt;
    }

    const auto input_it = object.find("input");
    if (input_it == object.end()) {
        return std::nullopt;
    }
    if (input_it->second.is_string()) {
        JsonArray items;
        items.push_back(user_text_item(std::string(input_it->second.get_string())));
        return items;
    }
    if (!input_it->second.is_array()) {
        return std::nullopt;
    }

    JsonArray items;
    for (const auto& item : input_it->second.get_array()) {
        if (is_function_call_output_item(item)) {
            return std::nullopt;
        }
        items.push_back(item);
    }
    return items;
}

bool request_input_looks_contextual(const JsonObject& object) {
    const auto input_it = object.find("input");
    if (input_it == object.end() || !input_it->second.is_array()) {
        return false;
    }
    const auto& items = input_it->second.get_array();
    if (items.size() > 1) {
        return true;
    }
    if (items.empty() || !items.front().is_object()) {
        return false;
    }
    const auto role = json_string_field(items.front().get_object(), "role");
    return role.has_value() && *role != "user";
}

std::optional<JsonArray> recovery_items_from_json(std::string_view raw_json) {
    if (raw_json.empty()) {
        return std::nullopt;
    }
    Json parsed;
    if (const auto ec = glz::read_json(parsed, raw_json); ec || !parsed.is_array()) {
        return std::nullopt;
    }
    return parsed.get_array();
}

std::optional<std::string> serialize_recovery_items(const JsonArray& items) {
    Json payload = items;
    const auto serialized = glz::write_json(payload);
    if (!serialized) {
        return std::nullopt;
    }
    return serialized.value_or("[]");
}

void trim_recovery_items(JsonArray& items) {
    while (items.size() > kMaxRecoveryInputItems) {
        items.erase(items.begin());
    }

    while (!items.empty()) {
        const auto serialized = serialize_recovery_items(items);
        if (!serialized.has_value() || serialized->size() <= kMaxRecoveryInputBytes) {
            return;
        }
        items.erase(items.begin());
    }
}

void append_items(JsonArray& target, const JsonArray& source) {
    for (const auto& item : source) {
        target.push_back(item);
    }
}

std::optional<Json> assistant_item_from_response_object(const JsonObject& response) {
    const auto output_it = response.find("output");
    if (output_it == response.end() || !output_it->second.is_array()) {
        return std::nullopt;
    }

    Json content = JsonArray{};
    auto& content_items = content.get_array();
    for (const auto& output_item : output_it->second.get_array()) {
        if (!output_item.is_object()) {
            continue;
        }
        const auto& output_object = output_item.get_object();
        const auto output_type = json_string_field(output_object, "type");
        if (output_type.has_value() && *output_type != "message" && *output_type != "output_text") {
            continue;
        }

        if (const auto text = json_string_field(output_object, "text"); text.has_value() && !text->empty()) {
            content_items.push_back(text_part("output_text", *text));
        }

        const auto output_content_it = output_object.find("content");
        if (output_content_it == output_object.end()) {
            continue;
        }
        if (output_content_it->second.is_string()) {
            content_items.push_back(text_part("output_text", std::string(output_content_it->second.get_string())));
            continue;
        }
        if (!output_content_it->second.is_array()) {
            continue;
        }
        for (const auto& part : output_content_it->second.get_array()) {
            if (!part.is_object()) {
                continue;
            }
            const auto& part_object = part.get_object();
            const auto part_type = json_string_field(part_object, "type");
            if (part_type.has_value() && *part_type != "output_text" && *part_type != "text" &&
                *part_type != "refusal") {
                continue;
            }
            if (const auto text = json_string_field(part_object, "text"); text.has_value() && !text->empty()) {
                content_items.push_back(text_part("output_text", *text));
            } else if (const auto refusal = json_string_field(part_object, "refusal");
                       refusal.has_value() && !refusal->empty()) {
                content_items.push_back(text_part("output_text", *refusal));
            }
        }
    }

    if (content_items.empty()) {
        return std::nullopt;
    }

    Json item = JsonObject{};
    item["role"] = "assistant";
    item["content"] = std::move(content);
    return item;
}

std::optional<Json> assistant_item_from_json_body(const std::string& response_body) {
    Json payload;
    if (const auto ec = glz::read_json(payload, response_body); ec || !payload.is_object()) {
        return std::nullopt;
    }
    const auto& object = payload.get_object();
    if (const auto item = assistant_item_from_response_object(object); item.has_value()) {
        return item;
    }

    const auto response_it = object.find("response");
    if (response_it != object.end() && response_it->second.is_object()) {
        return assistant_item_from_response_object(response_it->second.get_object());
    }
    return std::nullopt;
}

std::optional<Json> assistant_item_from_events(const std::vector<std::string>& events) {
    std::optional<Json> fallback;
    for (const auto& event : events) {
        Json payload;
        if (const auto ec = glz::read_json(payload, event); ec || !payload.is_object()) {
            continue;
        }

        const auto& object = payload.get_object();
        const auto type = json_string_field(object, "type");
        if (!type.has_value()) {
            continue;
        }
        if (*type != "response.completed" && *type != "response.incomplete" && *type != "response.created") {
            continue;
        }
        const auto response_it = object.find("response");
        if (response_it == object.end() || !response_it->second.is_object()) {
            continue;
        }

        auto item = assistant_item_from_response_object(response_it->second.get_object());
        if (!item.has_value()) {
            continue;
        }
        if (*type == "response.completed" || *type == "response.incomplete") {
            return item;
        }
        fallback = std::move(item);
    }
    return fallback;
}

std::optional<db::ProxyResponseContinuityRecord>
find_response_context_record(const std::string& previous_response_id, const openai::HeaderMap& headers) {
    const auto key = continuity_key_from_headers(headers);
    if (!key.has_value() || key->empty()) {
        return std::nullopt;
    }

    const auto api_key_scope = api_key_scope_from_headers(headers);
    std::lock_guard<std::mutex> lock(http_bridge_mutex());
    auto& persistence = bridge_persistence_state();
    const auto now = now_ms();
    if (sqlite3* db = ensure_persistence_db(persistence); db != nullptr) {
        auto record = db::find_proxy_response_continuity_by_response_id(db, previous_response_id, api_key_scope, now);
        maybe_purge_expired_persistence_rows(persistence, db, now);
        if (record.has_value() && record->continuity_key == *key) {
            return record;
        }
    }
    return std::nullopt;
}

std::optional<std::string> build_recovery_input_json(
    const std::string& request_body,
    const openai::HeaderMap& headers,
    const std::optional<Json>& assistant_item
) {
    Json payload;
    if (const auto ec = glz::read_json(payload, request_body); ec || !payload.is_object()) {
        return std::nullopt;
    }
    const auto& object = payload.get_object();
    auto current_input = request_input_items_for_recovery(object);
    if (!current_input.has_value()) {
        return std::nullopt;
    }

    JsonArray recovered_items;
    if (const auto previous_response_id = parse_previous_response_id(request_body); previous_response_id.has_value()) {
        if (const auto record = find_response_context_record(*previous_response_id, headers); record.has_value()) {
            if (auto previous_items = recovery_items_from_json(record->recovery_input_json); previous_items.has_value()) {
                recovered_items = std::move(*previous_items);
            }
        }
    }

    append_items(recovered_items, *current_input);
    if (assistant_item.has_value()) {
        recovered_items.push_back(*assistant_item);
    }
    trim_recovery_items(recovered_items);
    return serialize_recovery_items(recovered_items);
}

std::optional<std::string> rebuild_request_body_with_recovery_items(
    const std::string& raw_request_body,
    const JsonArray& recovery_items
) {
    Json payload;
    if (const auto ec = glz::read_json(payload, raw_request_body); ec || !payload.is_object()) {
        return std::nullopt;
    }
    auto object = payload.get_object();
    object.erase("previous_response_id");
    Json input = recovery_items;
    object["input"] = std::move(input);
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

void remember_response_id(
    const openai::HeaderMap& headers,
    const std::optional<std::string>& response_id,
    std::string_view resolved_account_id,
    const std::string_view recovery_input_json = {}
) {
    if (!response_id.has_value() || response_id->empty()) {
        return;
    }
    const auto key = continuity_key_from_headers(headers);
    if (!key.has_value() || key->empty()) {
        return;
    }

    auto account_id = std::string(core::text::trim_ascii(resolved_account_id));
    if (account_id.empty()) {
        account_id = account_id_from_headers(headers);
    }
    const auto api_key_scope = api_key_scope_from_headers(headers);
    const auto bridge_key = scoped_bridge_key(*key, api_key_scope);

    std::lock_guard<std::mutex> lock(http_bridge_mutex());
    auto& bridge = http_bridge();
    auto& persistence = bridge_persistence_state();
    const auto now = now_ms();
    bridge.upsert(bridge_key, *response_id, now);
    (void)bridge.purge_stale(now);

    if (sqlite3* db = ensure_persistence_db(persistence); db != nullptr) {
        (void)db::upsert_proxy_response_continuity(
            db,
            *key,
            api_key_scope,
            *response_id,
            account_id,
            now,
            kBridgeTtlMs,
            recovery_input_json
        );
        maybe_purge_expired_persistence_rows(persistence, db, now);
    }
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

    const auto api_key_scope = api_key_scope_from_headers(headers);
    const auto bridge_key = scoped_bridge_key(*key, api_key_scope);
    std::optional<std::string> bridged_previous_response_id;

    std::lock_guard<std::mutex> lock(http_bridge_mutex());
    auto& bridge = http_bridge();
    auto& persistence = bridge_persistence_state();
    const auto now = now_ms();

    if (const auto* existing = bridge.find(bridge_key, now); existing != nullptr) {
        bridged_previous_response_id = existing->upstream_session_id;
    }
    if (!bridged_previous_response_id.has_value() || bridged_previous_response_id->empty()) {
        if (sqlite3* db = ensure_persistence_db(persistence); db != nullptr) {
            if (const auto persisted = db::find_proxy_response_continuity(db, *key, api_key_scope, now);
                persisted.has_value() && !persisted->response_id.empty()) {
                bridged_previous_response_id = persisted->response_id;
                bridge.upsert(bridge_key, *bridged_previous_response_id, now);
            }
            maybe_purge_expired_persistence_rows(persistence, db, now);
        }
    }
    (void)bridge.purge_stale(now);

    if (!bridged_previous_response_id.has_value() || bridged_previous_response_id->empty()) {
        return raw_request_body;
    }

    const auto injected = inject_previous_response_id(raw_request_body, *bridged_previous_response_id);
    if (!injected.has_value()) {
        return raw_request_body;
    }
    return *injected;
}

std::optional<std::string>
replace_previous_response_id_from_bridge(const std::string& raw_request_body, const openai::HeaderMap& headers) {
    const auto existing_previous_response_id = parse_previous_response_id(raw_request_body);
    if (!existing_previous_response_id.has_value()) {
        return std::nullopt;
    }

    const auto key = continuity_key_from_headers(headers);
    if (!key.has_value() || key->empty()) {
        return std::nullopt;
    }

    const auto api_key_scope = api_key_scope_from_headers(headers);
    const auto bridge_key = scoped_bridge_key(*key, api_key_scope);
    std::optional<std::string> bridged_previous_response_id;

    {
        std::lock_guard<std::mutex> lock(http_bridge_mutex());
        auto& bridge = http_bridge();
        auto& persistence = bridge_persistence_state();
        const auto now = now_ms();

        if (const auto* existing = bridge.find(bridge_key, now); existing != nullptr) {
            bridged_previous_response_id = existing->upstream_session_id;
        }
        if (!bridged_previous_response_id.has_value() || bridged_previous_response_id->empty()) {
            if (sqlite3* db = ensure_persistence_db(persistence); db != nullptr) {
                if (const auto persisted = db::find_proxy_response_continuity(db, *key, api_key_scope, now);
                    persisted.has_value() && !persisted->response_id.empty()) {
                    bridged_previous_response_id = persisted->response_id;
                    bridge.upsert(bridge_key, *bridged_previous_response_id, now);
                }
                maybe_purge_expired_persistence_rows(persistence, db, now);
            }
        }
        (void)bridge.purge_stale(now);
    }

    if (!bridged_previous_response_id.has_value() || bridged_previous_response_id->empty()) {
        return std::nullopt;
    }
    if (*bridged_previous_response_id == *existing_previous_response_id) {
        return std::nullopt;
    }

    return inject_previous_response_id(raw_request_body, *bridged_previous_response_id);
}

std::optional<std::string> strip_previous_response_id(const std::string& raw_request_body) {
    return strip_previous_response_id_impl(raw_request_body);
}

bool request_contains_function_call_output(const std::string& raw_request_body) {
    Json payload;
    if (const auto ec = glz::read_json(payload, raw_request_body); ec || !payload.is_object()) {
        return false;
    }
    return object_contains_function_call_output(payload.get_object());
}

std::optional<std::string>
resolve_preferred_account_id_from_previous_response(const std::string& raw_request_body, const openai::HeaderMap& headers) {
    const auto previous_response_id = parse_previous_response_id(raw_request_body);
    if (!previous_response_id.has_value()) {
        return std::nullopt;
    }

    const auto key = continuity_key_from_headers(headers);
    if (!key.has_value() || key->empty()) {
        return std::nullopt;
    }
    const auto api_key_scope = api_key_scope_from_headers(headers);

    std::lock_guard<std::mutex> lock(http_bridge_mutex());
    auto& persistence = bridge_persistence_state();
    const auto now = now_ms();
    if (sqlite3* db = ensure_persistence_db(persistence); db != nullptr) {
        std::optional<std::string> account_id;
        if (const auto record = db::find_proxy_response_continuity(db, *key, api_key_scope, now);
            record.has_value() && record->response_id == *previous_response_id && !record->account_id.empty()) {
            account_id = record->account_id;
        }
        maybe_purge_expired_persistence_rows(persistence, db, now);
        return account_id;
    }
    return std::nullopt;
}

std::optional<std::string> resolve_continuity_account_id(const openai::HeaderMap& headers) {
    const auto key = continuity_key_from_headers(headers);
    if (!key.has_value() || key->empty()) {
        return std::nullopt;
    }

    const auto api_key_scope = api_key_scope_from_headers(headers);
    std::lock_guard<std::mutex> lock(http_bridge_mutex());
    auto& persistence = bridge_persistence_state();
    const auto now = now_ms();
    if (sqlite3* db = ensure_persistence_db(persistence); db != nullptr) {
        if (const auto record = db::find_proxy_response_continuity(db, *key, api_key_scope, now);
            record.has_value() && !record->account_id.empty()) {
            maybe_purge_expired_persistence_rows(persistence, db, now);
            return record->account_id;
        }
        maybe_purge_expired_persistence_rows(persistence, db, now);
    }
    return std::nullopt;
}

bool request_has_previous_response_id(const std::string& raw_request_body) {
    return parse_previous_response_id(raw_request_body).has_value();
}

std::optional<std::string>
rebuild_request_body_with_local_context(const std::string& raw_request_body, const openai::HeaderMap& headers) {
    if (request_contains_function_call_output(raw_request_body)) {
        return std::nullopt;
    }

    const auto previous_response_id = parse_previous_response_id(raw_request_body);
    if (!previous_response_id.has_value()) {
        return std::nullopt;
    }

    Json payload;
    if (const auto ec = glz::read_json(payload, raw_request_body); ec || !payload.is_object()) {
        return std::nullopt;
    }
    const auto& object = payload.get_object();
    auto current_input = request_input_items_for_recovery(object);
    if (!current_input.has_value()) {
        return std::nullopt;
    }

    if (request_input_looks_contextual(object)) {
        return strip_previous_response_id(raw_request_body);
    }

    const auto record = find_response_context_record(*previous_response_id, headers);
    if (!record.has_value()) {
        return std::nullopt;
    }
    auto recovery_items = recovery_items_from_json(record->recovery_input_json);
    if (!recovery_items.has_value() || recovery_items->empty()) {
        return std::nullopt;
    }

    append_items(*recovery_items, *current_input);
    trim_recovery_items(*recovery_items);
    return rebuild_request_body_with_recovery_items(raw_request_body, *recovery_items);
}

void remember_response_id_from_json(
    const openai::HeaderMap& headers,
    const std::string& response_body,
    const std::string_view account_id
) {
    remember_response_id(headers, response_id_from_json_body(response_body), account_id);
}

void remember_response_id_from_json(
    const openai::HeaderMap& headers,
    const std::string& request_body,
    const std::string& response_body,
    const std::string_view account_id
) {
    const auto recovery_input_json =
        build_recovery_input_json(request_body, headers, assistant_item_from_json_body(response_body));
    remember_response_id(
        headers,
        response_id_from_json_body(response_body),
        account_id,
        recovery_input_json.value_or(std::string{})
    );
}

void remember_response_id_from_events(
    const openai::HeaderMap& headers,
    const std::vector<std::string>& events,
    const std::string_view account_id
) {
    remember_response_id(headers, response_id_from_events(events), account_id);
}

void remember_response_id_from_events(
    const openai::HeaderMap& headers,
    const std::string& request_body,
    const std::vector<std::string>& events,
    const std::string_view account_id
) {
    const auto recovery_input_json = build_recovery_input_json(request_body, headers, assistant_item_from_events(events));
    remember_response_id(
        headers,
        response_id_from_events(events),
        account_id,
        recovery_input_json.value_or(std::string{})
    );
}

void reset_response_bridge_for_tests() {
    std::lock_guard<std::mutex> lock(http_bridge_mutex());
    http_bridge().clear();
}

} // namespace tightrope::proxy::session
