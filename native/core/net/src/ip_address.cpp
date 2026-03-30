#include "ip_address.h"

#include <boost/asio/ip/address.hpp>
#include <boost/system/error_code.hpp>

#include "text/ascii.h"

namespace tightrope::core::net {

namespace {

std::optional<boost::asio::ip::address> parse_address(std::string_view value) noexcept {
    const auto trimmed = core::text::trim_ascii(value);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    boost::system::error_code ec;
    const auto address = boost::asio::ip::make_address(trimmed, ec);
    if (ec) {
        return std::nullopt;
    }
    return address;
}

bool is_ip_in_network_v4(const boost::asio::ip::address_v4 address, const boost::asio::ip::network_v4& network
) noexcept {
    const auto mask = network.netmask().to_uint();
    const auto network_bits = network.network().to_uint();
    return (address.to_uint() & mask) == network_bits;
}

bool is_ip_in_network_v6(const boost::asio::ip::address_v6& address, const boost::asio::ip::network_v6& network
) noexcept {
    const auto address_bytes = address.to_bytes();
    const auto network_bytes = network.network().to_bytes();
    const auto prefix_length = network.prefix_length();

    const auto full_bytes = static_cast<std::size_t>(prefix_length / 8);
    const auto partial_bits = static_cast<unsigned>(prefix_length % 8);

    for (std::size_t index = 0; index < full_bytes; ++index) {
        if (address_bytes[index] != network_bytes[index]) {
            return false;
        }
    }

    if (partial_bits == 0) {
        return true;
    }

    const auto mask = static_cast<unsigned char>(0xFFu << (8u - partial_bits));
    return (address_bytes[full_bytes] & mask) == (network_bytes[full_bytes] & mask);
}

} // namespace

std::optional<std::string> normalize_ip_address(std::string_view value) noexcept {
    const auto parsed = parse_address(value);
    if (!parsed.has_value()) {
        return std::nullopt;
    }
    return parsed->to_string();
}

bool is_valid_ip_address(std::string_view value) noexcept {
    return parse_address(value).has_value();
}

std::optional<IpNetwork> parse_ip_network(std::string_view cidr) noexcept {
    const auto trimmed = core::text::trim_ascii(cidr);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    boost::system::error_code ec;
    const auto v4 = boost::asio::ip::make_network_v4(trimmed, ec);
    if (!ec) {
        return IpNetwork{v4};
    }

    ec.clear();
    const auto v6 = boost::asio::ip::make_network_v6(trimmed, ec);
    if (!ec) {
        return IpNetwork{v6};
    }
    return std::nullopt;
}

bool is_ip_in_network(std::string_view ip_address, const IpNetwork& network) noexcept {
    const auto parsed = parse_address(ip_address);
    if (!parsed.has_value()) {
        return false;
    }

    if (std::holds_alternative<boost::asio::ip::network_v4>(network)) {
        if (!parsed->is_v4()) {
            return false;
        }
        return is_ip_in_network_v4(parsed->to_v4(), std::get<boost::asio::ip::network_v4>(network));
    }

    if (!parsed->is_v6()) {
        return false;
    }
    return is_ip_in_network_v6(parsed->to_v6(), std::get<boost::asio::ip::network_v6>(network));
}

} // namespace tightrope::core::net
