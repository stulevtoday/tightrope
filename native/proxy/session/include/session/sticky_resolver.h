#pragma once
// Sticky session key resolution

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tightrope::proxy::session {

struct StickySelection {
    std::string key;
    std::string account_id;
    bool reused = false;
};

class StickyResolver {
public:
    explicit StickyResolver(std::vector<std::string> account_ids);

    StickySelection pick(std::string_view key);
    bool erase(std::string_view key);
    void clear();

private:
    std::vector<std::string> account_ids_;
    std::unordered_map<std::string, std::string> sticky_map_;
    std::size_t cursor_ = 0;
};

} // namespace tightrope::proxy::session
