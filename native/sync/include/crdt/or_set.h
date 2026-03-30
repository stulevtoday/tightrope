#pragma once
// Observed-remove set (add-wins)

#include <cstdint>
#include <string>
#include <vector>

#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>

namespace tightrope::sync::crdt {

struct ORSetTag {
    std::uint32_t site_id = 0;
    std::uint64_t counter = 0;

    bool operator<(const ORSetTag& other) const {
        if (site_id != other.site_id) {
            return site_id < other.site_id;
        }
        return counter < other.counter;
    }

    bool operator==(const ORSetTag& other) const {
        return site_id == other.site_id && counter == other.counter;
    }
};

class ORSet {
public:
    using Tag = ORSetTag;
    using TagSet = boost::container::flat_set<Tag>;
    using ElementMap = boost::container::flat_map<std::string, TagSet>;
    using CounterMap = boost::container::flat_map<std::uint32_t, std::uint64_t>;

    Tag next_tag(const std::uint32_t site_id) {
        auto& counter = counters_[site_id];
        ++counter;
        return {.site_id = site_id, .counter = counter};
    }

    void add(const std::string& element, const std::uint32_t site_id) {
        elements_[element].insert(next_tag(site_id));
    }

    void add_tag(const std::string& element, const Tag& tag) {
        elements_[element].insert(tag);
    }

    void set_counter(const std::uint32_t site_id, const std::uint64_t counter) {
        auto& current = counters_[site_id];
        if (counter > current) {
            current = counter;
        }
    }

    void remove(const std::string& element) {
        elements_.erase(element);
    }

    void merge(const ORSet& other) {
        for (const auto& [site_id, counter] : other.counters_) {
            set_counter(site_id, counter);
        }
        for (const auto& [element, tags] : other.elements_) {
            auto& own_tags = elements_[element];
            own_tags.insert(tags.begin(), tags.end());
        }
    }

    bool contains(const std::string& element) const {
        const auto it = elements_.find(element);
        return it != elements_.end() && !it->second.empty();
    }

    std::vector<std::string> values() const {
        std::vector<std::string> out;
        out.reserve(elements_.size());
        for (const auto& [element, tags] : elements_) {
            if (!tags.empty()) {
                out.push_back(element);
            }
        }
        return out;
    }

    const ElementMap& elements() const {
        return elements_;
    }

    const CounterMap& counters() const {
        return counters_;
    }

private:
    ElementMap elements_;
    CounterMap counters_;
};

} // namespace tightrope::sync::crdt
