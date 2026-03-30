#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "upstream_headers.h"

// Model capability lookups

namespace tightrope::proxy::openai {

struct ModelInfo {
    std::string id;
    bool supports_streaming = true;
    bool supported_in_api = true;
    bool prefer_websockets = false;
};

class ModelRegistry {
public:
    ModelRegistry() = default;
    explicit ModelRegistry(std::vector<ModelInfo> models);

    bool has_model(std::string_view id) const;
    const ModelInfo* find_model(std::string_view id) const;
    bool prefers_websockets(std::string_view id) const;
    const std::vector<ModelInfo>& list_models() const;

private:
    std::vector<ModelInfo> models_;
    std::unordered_map<std::string, std::size_t> model_index_;
};

ModelRegistry build_default_model_registry();
ModelRegistry build_model_registry_from_upstream(const HeaderMap& inbound_headers);

} // namespace tightrope::proxy::openai
