#include "keys_controller.h"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "api_keys/key_validator.h"
#include "api_keys/limit_enforcer.h"
#include "controller_db.h"
#include "text/ascii.h"
#include "repositories/api_key_repo.h"

namespace tightrope::server::controllers {

namespace {

ApiKeyMutationResponse invalid_payload(std::string_view message) {
    return {
        .status = 400,
        .code = "invalid_api_key_payload",
        .message = std::string(message),
    };
}

ApiKeyMutationResponse not_found() {
    return {
        .status = 404,
        .code = "api_key_not_found",
        .message = "API key not found",
    };
}

std::optional<std::string> normalize_optional_slug(const std::optional<std::string>& value) {
    if (!value.has_value()) {
        return std::nullopt;
    }
    const auto trimmed = core::text::trim_ascii(*value);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    return trimmed;
}

std::optional<std::vector<db::ApiKeyLimitInput>> normalize_limits(const std::vector<ApiKeyLimitRuleInput>& limits) {
    std::vector<db::ApiKeyLimitInput> normalized;
    normalized.reserve(limits.size());
    for (const auto& limit : limits) {
        if (!auth::api_keys::is_supported_limit_type(limit.limit_type) ||
            !auth::api_keys::window_to_period_seconds(limit.limit_window).has_value() || limit.max_value <= 0.0) {
            return std::nullopt;
        }
        normalized.push_back(
            db::ApiKeyLimitInput{
                .limit_type = limit.limit_type,
                .limit_window = limit.limit_window,
                .max_value = limit.max_value,
                .model_filter = limit.model_filter,
                .current_value = 0.0,
            }
        );
    }
    return normalized;
}

ApiKeyPayload to_payload(const db::ApiKeyRecord& row) {
    ApiKeyPayload payload;
    payload.key_id = row.key_id;
    payload.name = row.name;
    payload.key_prefix = row.key_prefix;
    payload.is_active = row.is_active;
    payload.allowed_models = auth::api_keys::deserialize_allowed_models(row.allowed_models);
    payload.enforced_model = row.enforced_model;
    payload.enforced_reasoning_effort = row.enforced_reasoning_effort;
    payload.expires_at = row.expires_at;
    payload.created_at = row.created_at;
    payload.last_used_at = row.last_used_at;
    for (const auto& limit : row.limits) {
        payload.limits.push_back(
            ApiKeyLimitPayload{
                .id = limit.id,
                .limit_type = limit.limit_type,
                .limit_window = limit.limit_window,
                .max_value = limit.max_value,
                .current_value = limit.current_value,
                .model_filter = limit.model_filter,
            }
        );
    }
    return payload;
}

std::string generate_key_id() {
    static thread_local boost::uuids::random_generator generator;
    return boost::uuids::to_string(generator());
}

} // namespace

ApiKeyMutationResponse create_api_key(const ApiKeyCreateRequest& request, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }

    const auto name = core::text::trim_ascii(request.name);
    if (name.empty()) {
        return invalid_payload("API key name is required");
    }

    const auto allowed_models = auth::api_keys::normalize_allowed_models(request.allowed_models);
    const auto enforced_model = normalize_optional_slug(request.enforced_model);
    if (!auth::api_keys::validate_model_enforcement(enforced_model, allowed_models)) {
        return invalid_payload("enforced_model must be present in allowed_models when allowed_models is configured");
    }

    std::optional<std::string> normalized_reasoning = std::string{};
    if (request.enforced_reasoning_effort.has_value()) {
        normalized_reasoning = auth::api_keys::normalize_reasoning_effort(*request.enforced_reasoning_effort);
        if (!normalized_reasoning.has_value()) {
            return invalid_payload("Unsupported enforced reasoning effort");
        }
    } else {
        normalized_reasoning = std::nullopt;
    }

    const auto normalized_limits = normalize_limits(request.limits);
    if (!normalized_limits.has_value()) {
        return invalid_payload("Invalid limit definition");
    }

    const auto material = auth::api_keys::issue_api_key_material();
    if (!material.has_value()) {
        return {
            .status = 500,
            .code = "api_key_generation_failed",
            .message = "Failed to generate API key",
        };
    }

    const auto created = db::create_api_key(
        handle.db,
        generate_key_id(),
        material->key_hash,
        material->key_prefix,
        name,
        auth::api_keys::serialize_allowed_models(allowed_models),
        enforced_model,
        (normalized_reasoning.has_value() && !normalized_reasoning->empty()) ? normalized_reasoning : std::nullopt,
        request.expires_at
    );
    if (!created.has_value()) {
        return {
            .status = 500,
            .code = "api_key_create_failed",
            .message = "Failed to create API key",
        };
    }

    if (!normalized_limits->empty()) {
        if (!db::replace_api_key_limits(handle.db, created->key_id, *normalized_limits).has_value()) {
            return {
                .status = 500,
                .code = "api_key_limits_failed",
                .message = "Failed to store API key limits",
            };
        }
    }

    const auto refreshed = db::get_api_key_by_key_id(handle.db, created->key_id);
    if (!refreshed.has_value()) {
        return {
            .status = 500,
            .code = "api_key_create_failed",
            .message = "Failed to load API key",
        };
    }

    return {
        .status = 201,
        .key = material->plain_key,
        .api_key = to_payload(*refreshed),
    };
}

ApiKeyListResponse list_api_keys(sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }

    ApiKeyListResponse response{.status = 200};
    for (const auto& row : db::list_api_keys(handle.db)) {
        response.items.push_back(to_payload(row));
    }
    return response;
}

ApiKeyMutationResponse update_api_key(const std::string_view key_id, const ApiKeyUpdateRequest& request, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }

    const auto existing = db::get_api_key_by_key_id(handle.db, key_id);
    if (!existing.has_value()) {
        return not_found();
    }

    const auto enforced_model =
        request.enforced_model.has_value() ? normalize_optional_slug(request.enforced_model) : existing->enforced_model;
    const auto allowed_models = request.allowed_models.has_value()
                                    ? auth::api_keys::normalize_allowed_models(request.allowed_models)
                                    : auth::api_keys::deserialize_allowed_models(existing->allowed_models);
    if (!auth::api_keys::validate_model_enforcement(enforced_model, allowed_models)) {
        return invalid_payload("enforced_model must be present in allowed_models when allowed_models is configured");
    }

    std::optional<std::string> normalized_reasoning = existing->enforced_reasoning_effort;
    if (request.enforced_reasoning_effort.has_value()) {
        normalized_reasoning = auth::api_keys::normalize_reasoning_effort(*request.enforced_reasoning_effort);
        if (!normalized_reasoning.has_value()) {
            return invalid_payload("Unsupported enforced reasoning effort");
        }
    }

    db::ApiKeyPatch patch;
    if (request.name.has_value()) {
        const auto trimmed = core::text::trim_ascii(*request.name);
        if (trimmed.empty()) {
            return invalid_payload("API key name is required");
        }
        patch.name = trimmed;
    }
    patch.is_active = request.is_active;
    patch.expires_at = request.expires_at;
    if (request.allowed_models.has_value()) {
        patch.allowed_models = auth::api_keys::serialize_allowed_models(allowed_models).value_or("");
    }
    if (request.enforced_model.has_value()) {
        patch.enforced_model = enforced_model.value_or("");
    }
    if (request.enforced_reasoning_effort.has_value()) {
        patch.enforced_reasoning_effort = normalized_reasoning.value_or("");
    }

    const auto updated = db::update_api_key(handle.db, key_id, patch);
    if (!updated.has_value()) {
        return not_found();
    }

    if (request.limits.has_value()) {
        const auto normalized_limits = normalize_limits(*request.limits);
        if (!normalized_limits.has_value()) {
            return invalid_payload("Invalid limit definition");
        }
        if (!db::replace_api_key_limits(handle.db, key_id, *normalized_limits).has_value()) {
            return {
                .status = 500,
                .code = "api_key_limits_failed",
                .message = "Failed to store API key limits",
            };
        }
    }

    const auto refreshed = db::get_api_key_by_key_id(handle.db, key_id);
    if (!refreshed.has_value()) {
        return not_found();
    }
    return {
        .status = 200,
        .api_key = to_payload(*refreshed),
    };
}

ApiKeyMutationResponse regenerate_api_key(const std::string_view key_id, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }

    const auto material = auth::api_keys::issue_api_key_material();
    if (!material.has_value()) {
        return {
            .status = 500,
            .code = "api_key_generation_failed",
            .message = "Failed to generate API key",
        };
    }
    const auto updated = db::rotate_api_key_secret(handle.db, key_id, material->key_hash, material->key_prefix);
    if (!updated.has_value()) {
        return not_found();
    }
    return {
        .status = 200,
        .key = material->plain_key,
        .api_key = to_payload(*updated),
    };
}

ApiKeyDeleteResponse delete_api_key(const std::string_view key_id, sqlite3* db) {
    auto handle = open_controller_db(db);
    if (handle.db == nullptr) {
        return {
            .status = 500,
            .code = "db_unavailable",
            .message = "Database unavailable",
        };
    }
    if (!db::delete_api_key(handle.db, key_id)) {
        return {
            .status = 404,
            .code = "api_key_not_found",
            .message = "API key not found",
        };
    }
    return {.status = 204};
}

} // namespace tightrope::server::controllers
