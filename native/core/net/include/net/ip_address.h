#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <boost/asio/ip/network_v4.hpp>
#include <boost/asio/ip/network_v6.hpp>

namespace tightrope::core::net {

using IpNetwork = std::variant<boost::asio::ip::network_v4, boost::asio::ip::network_v6>;

[[nodiscard]] std::optional<std::string> normalize_ip_address(std::string_view value) noexcept;
[[nodiscard]] bool is_valid_ip_address(std::string_view value) noexcept;
[[nodiscard]] std::optional<IpNetwork> parse_ip_network(std::string_view cidr) noexcept;
[[nodiscard]] bool is_ip_in_network(std::string_view ip_address, const IpNetwork& network) noexcept;

} // namespace tightrope::core::net
