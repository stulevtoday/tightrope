#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace tightrope::sync::discovery {

constexpr std::size_t k_mdns_buffer_bytes = 2048;
using MdnsAlignedBuffer = std::array<std::uint32_t, k_mdns_buffer_bytes / sizeof(std::uint32_t)>;

inline void* mdns_buffer_data(MdnsAlignedBuffer& buffer) {
    return static_cast<void*>(buffer.data());
}

inline const void* mdns_buffer_data(const MdnsAlignedBuffer& buffer) {
    return static_cast<const void*>(buffer.data());
}

inline std::size_t mdns_buffer_size(const MdnsAlignedBuffer& buffer) {
    return buffer.size() * sizeof(std::uint32_t);
}

std::string canonical_mdns_service_name(std::string_view service_name);
std::string canonical_mdns_hostname(std::string_view host, std::uint32_t site_id, bool host_is_ip_literal);
std::string trim_mdns_trailing_dot(std::string_view value);
std::string lowercase_ascii(std::string_view value);

} // namespace tightrope::sync::discovery
