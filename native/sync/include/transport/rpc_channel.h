#pragma once
// Multiplexed RPC over TLS (Raft + sync + CRDT)

#include <cstdint>
#include <optional>
#include <vector>

namespace tightrope::sync::transport {

struct RpcFrame {
    std::uint16_t channel = 0;
    std::vector<std::uint8_t> payload;
};

class RpcChannel {
public:
    static std::vector<std::uint8_t> encode(const RpcFrame& frame);
    static std::optional<RpcFrame> try_decode(std::vector<std::uint8_t>& buffer);
};

} // namespace tightrope::sync::transport
