#include "discovery/peer_endpoint.h"

#include <charconv>
#include <cctype>
#include <limits>

#include <boost/asio/ip/address.hpp>

namespace tightrope::sync::discovery {

namespace {

bool is_hostname_char(const char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '-';
}

bool is_valid_hostname(const std::string& host) {
    if (host.empty() || host.size() > 255) {
        return false;
    }
    if (host.front() == '.' || host.back() == '.' || host.front() == '-' || host.back() == '-') {
        return false;
    }
    for (const auto c : host) {
        if (!is_hostname_char(c)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool is_valid_endpoint(const PeerEndpoint& endpoint) {
    if (endpoint.port == 0 || endpoint.host.empty()) {
        return false;
    }

    boost::system::error_code ec;
    boost::asio::ip::make_address(endpoint.host, ec);
    if (!ec) {
        return true;
    }
    return is_valid_hostname(endpoint.host);
}

std::optional<PeerEndpoint> parse_endpoint(const std::string_view address) {
    if (address.empty()) {
        return std::nullopt;
    }

    std::string host;
    std::string_view port_view;

    if (address.front() == '[') {
        const auto close = address.find(']');
        if (close == std::string_view::npos || close + 2 > address.size() || address[close + 1] != ':') {
            return std::nullopt;
        }
        host = std::string(address.substr(1, close - 1));
        port_view = address.substr(close + 2);
    } else {
        const auto split = address.rfind(':');
        if (split == std::string_view::npos || split == 0 || split + 1 >= address.size()) {
            return std::nullopt;
        }
        host = std::string(address.substr(0, split));
        port_view = address.substr(split + 1);
    }

    std::uint32_t port_u32 = 0;
    const auto* first = port_view.data();
    const auto* last = port_view.data() + port_view.size();
    const auto parsed = std::from_chars(first, last, port_u32);
    if (parsed.ec != std::errc{} || parsed.ptr != last || port_u32 == 0 ||
        port_u32 > std::numeric_limits<std::uint16_t>::max()) {
        return std::nullopt;
    }

    PeerEndpoint endpoint = {
        .host = std::move(host),
        .port = static_cast<std::uint16_t>(port_u32),
    };
    if (!is_valid_endpoint(endpoint)) {
        return std::nullopt;
    }
    return endpoint;
}

std::string endpoint_to_string(const PeerEndpoint& endpoint) {
    if (endpoint.host.find(':') != std::string::npos) {
        return "[" + endpoint.host + "]:" + std::to_string(endpoint.port);
    }
    return endpoint.host + ":" + std::to_string(endpoint.port);
}

} // namespace tightrope::sync::discovery
