#pragma once
// Positive-negative counter (two G-Counters)

#include <cstdint>

#include "g_counter.h"

namespace tightrope::sync::crdt {

class PNCounter {
public:
    void add(const std::uint32_t site_id, const std::int64_t delta) {
        if (delta >= 0) {
            positive_.increment(site_id, delta);
            return;
        }
        negative_.increment(site_id, -delta);
    }

    void set_site_counts(const std::uint32_t site_id, const std::int64_t positive, const std::int64_t negative) {
        positive_.set_count(site_id, positive);
        negative_.set_count(site_id, negative);
    }

    std::int64_t value() const {
        return positive_.value() - negative_.value();
    }

    void merge(const PNCounter& other) {
        positive_.merge(other.positive_);
        negative_.merge(other.negative_);
    }

    const GCounter<>& positive() const {
        return positive_;
    }

    const GCounter<>& negative() const {
        return negative_;
    }

private:
    GCounter<> positive_;
    GCounter<> negative_;
};

} // namespace tightrope::sync::crdt
