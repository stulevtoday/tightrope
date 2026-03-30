#pragma once
// Last-writer-wins register with HLC

#include <cstdint>
#include <utility>

#include "../hlc.h"

namespace tightrope::sync::crdt {

template <typename T>
class LWWRegister {
public:
    void set(T new_value, const Hlc now, const std::uint32_t site_id) {
        value_ = std::move(new_value);
        timestamp_ = now;
        site_id_ = site_id;
        initialized_ = true;
    }

    void merge(const LWWRegister& other) {
        if (!other.initialized_) {
            return;
        }
        if (!initialized_) {
            *this = other;
            return;
        }

        const auto cmp = compare_hlc(other.timestamp_, timestamp_);
        if (cmp > 0 || (cmp == 0 && other.site_id_ > site_id_)) {
            *this = other;
        }
    }

    bool initialized() const {
        return initialized_;
    }

    const T& value() const {
        return value_;
    }

    const Hlc& timestamp() const {
        return timestamp_;
    }

    std::uint32_t site_id() const {
        return site_id_;
    }

private:
    T value_{};
    Hlc timestamp_{};
    std::uint32_t site_id_ = 0;
    bool initialized_ = false;
};

} // namespace tightrope::sync::crdt
