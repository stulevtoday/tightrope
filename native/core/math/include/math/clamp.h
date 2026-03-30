#pragma once

namespace tightrope::core::math {

template <typename T>
constexpr T clamp_non_negative(const T value) {
    return value < static_cast<T>(0) ? static_cast<T>(0) : value;
}

template <typename T>
constexpr T clamp_unit(const T value) {
    if (value < static_cast<T>(0)) {
        return static_cast<T>(0);
    }
    if (value > static_cast<T>(1)) {
        return static_cast<T>(1);
    }
    return value;
}

} // namespace tightrope::core::math
