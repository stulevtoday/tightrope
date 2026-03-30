#include "sticky_resolver.h"

#include <utility>

namespace tightrope::proxy::session {

StickyResolver::StickyResolver(std::vector<std::string> account_ids) : account_ids_(std::move(account_ids)) {}

StickySelection StickyResolver::pick(const std::string_view key) {
    const std::string stable_key(key);
    if (const auto it = sticky_map_.find(stable_key); it != sticky_map_.end()) {
        return {
            .key = stable_key,
            .account_id = it->second,
            .reused = true,
        };
    }

    if (account_ids_.empty()) {
        return {
            .key = stable_key,
            .account_id = "",
            .reused = false,
        };
    }

    const auto index = cursor_ % account_ids_.size();
    const std::string account_id = account_ids_[index];
    cursor_ = (index + 1) % account_ids_.size();
    sticky_map_.emplace(stable_key, account_id);
    return {
        .key = stable_key,
        .account_id = account_id,
        .reused = false,
    };
}

bool StickyResolver::erase(const std::string_view key) {
    return sticky_map_.erase(std::string(key)) > 0;
}

void StickyResolver::clear() {
    sticky_map_.clear();
    cursor_ = 0;
}

} // namespace tightrope::proxy::session
