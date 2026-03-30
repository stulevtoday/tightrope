#include "limit_enforcer.h"

#include <algorithm>

#include <glaze/glaze.hpp>

#include "text/ascii.h"

namespace tightrope::auth::api_keys {

bool is_supported_limit_type(const std::string_view limit_type) noexcept {
    return limit_type == "total_tokens" || limit_type == "input_tokens" || limit_type == "output_tokens" ||
           limit_type == "cost_usd";
}

std::optional<std::int64_t> window_to_period_seconds(const std::string_view limit_window) noexcept {
    if (limit_window == "daily") {
        return 86400;
    }
    if (limit_window == "weekly") {
        return 604800;
    }
    if (limit_window == "monthly") {
        return 2592000;
    }
    return std::nullopt;
}

std::optional<std::string> period_seconds_to_window(const std::int64_t period_seconds) noexcept {
    if (period_seconds == 86400) {
        return std::string("daily");
    }
    if (period_seconds == 604800) {
        return std::string("weekly");
    }
    if (period_seconds == 2592000) {
        return std::string("monthly");
    }
    return std::nullopt;
}

std::optional<std::string> normalize_reasoning_effort(const std::string_view value) noexcept {
    const auto normalized = core::text::to_lower_ascii(core::text::trim_ascii(value));
    if (normalized.empty()) {
        return std::string{};
    }

    if (normalized == "none" || normalized == "minimal" || normalized == "low" || normalized == "medium" ||
        normalized == "high" || normalized == "xhigh") {
        return normalized;
    }
    return std::nullopt;
}

std::vector<std::string> normalize_allowed_models(const std::optional<std::vector<std::string>>& models) {
    std::vector<std::string> normalized;
    if (!models.has_value()) {
        return normalized;
    }

    normalized.reserve(models->size());
    for (const auto& model : *models) {
        const auto trimmed = core::text::trim_ascii(model);
        if (!trimmed.empty()) {
            normalized.push_back(trimmed);
        }
    }
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    return normalized;
}

std::optional<std::string> serialize_allowed_models(const std::vector<std::string>& models) {
    if (models.empty()) {
        return std::nullopt;
    }
    const auto serialized = glz::write_json(models);
    if (!serialized) {
        return std::nullopt;
    }
    return serialized.value();
}

std::vector<std::string> deserialize_allowed_models(const std::optional<std::string>& encoded) {
    std::vector<std::string> models;
    if (!encoded.has_value() || encoded->empty()) {
        return models;
    }
    if (const auto ec = glz::read_json(models, *encoded); ec) {
        models.clear();
    }
    return models;
}

bool validate_model_enforcement(
    const std::optional<std::string>& enforced_model,
    const std::vector<std::string>& allowed_models
) noexcept {
    if (!enforced_model.has_value() || enforced_model->empty() || allowed_models.empty()) {
        return true;
    }
    return std::find(allowed_models.begin(), allowed_models.end(), *enforced_model) != allowed_models.end();
}

} // namespace tightrope::auth::api_keys
