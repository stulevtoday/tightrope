#pragma once
// Grow-only counter

#include <algorithm>
#include <cstdint>

#include <boost/container/flat_map.hpp>

namespace tightrope::sync::crdt {

template <typename K = std::uint32_t>
class GCounter {
public:
    using key_type = K;
    using map_type = boost::container::flat_map<key_type, std::int64_t>;

    void increment(const key_type site_id, const std::int64_t delta = 1) {
        if (delta <= 0) {
            return;
        }
        counts_[site_id] += delta;
    }

    void set_count(const key_type site_id, const std::int64_t absolute_value) {
        counts_[site_id] = std::max<std::int64_t>(absolute_value, 0);
    }

    std::int64_t value() const {
        std::int64_t sum = 0;
        for (const auto& [_, count] : counts_) {
            sum += count;
        }
        return sum;
    }

    void merge(const GCounter& other) {
        for (const auto& [site, count] : other.counts_) {
            const auto it = counts_.find(site);
            if (it == counts_.end()) {
                counts_.emplace(site, count);
                continue;
            }
            it->second = std::max(it->second, count);
        }
    }

    const map_type& counts() const {
        return counts_;
    }

private:
    map_type counts_;
};

} // namespace tightrope::sync::crdt
