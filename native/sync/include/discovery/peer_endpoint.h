#pragma once
// Shared peer endpoint validation helpers

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tightrope::sync::discovery {

struct PeerEndpoint {
    std::string host;
    std::uint16_t port = 0;
};

bool is_valid_endpoint(const PeerEndpoint& endpoint);
std::optional<PeerEndpoint> parse_endpoint(std::string_view address);
std::string endpoint_to_string(const PeerEndpoint& endpoint);

} // namespace tightrope::sync::discovery
