#include "transport/rpc_channel.h"

#include <cstddef>
#include <string>

#include <boost/asio/buffer.hpp>

#include "sync_logging.h"

namespace tightrope::sync::transport {

namespace {

constexpr std::size_t kHeaderSize = sizeof(std::uint16_t) + sizeof(std::uint32_t);

void write_u16(std::vector<std::uint8_t>& out, const std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void write_u32(std::vector<std::uint8_t>& out, const std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

std::uint16_t read_u16(const std::uint8_t* raw) {
    return static_cast<std::uint16_t>(raw[0]) | (static_cast<std::uint16_t>(raw[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* raw) {
    return static_cast<std::uint32_t>(raw[0]) | (static_cast<std::uint32_t>(raw[1]) << 8U) |
           (static_cast<std::uint32_t>(raw[2]) << 16U) | (static_cast<std::uint32_t>(raw[3]) << 24U);
}

} // namespace

std::vector<std::uint8_t> RpcChannel::encode(const RpcFrame& frame) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kHeaderSize + frame.payload.size());
    write_u16(bytes, frame.channel);
    write_u32(bytes, static_cast<std::uint32_t>(frame.payload.size()));
    bytes.insert(bytes.end(), frame.payload.begin(), frame.payload.end());
    log_sync_event(
        SyncLogLevel::Trace,
        "rpc_channel",
        "encode",
        "channel=" + std::to_string(frame.channel) + " payload_bytes=" + std::to_string(frame.payload.size()));
    return bytes;
}

std::optional<RpcFrame> RpcChannel::try_decode(std::vector<std::uint8_t>& buffer) {
    if (buffer.size() < kHeaderSize) {
        return std::nullopt;
    }

    const auto* raw = buffer.data();
    const auto channel = read_u16(raw);
    const auto payload_size = read_u32(raw + sizeof(std::uint16_t));
    const auto required = kHeaderSize + static_cast<std::size_t>(payload_size);
    if (buffer.size() < required) {
        return std::nullopt;
    }

    RpcFrame frame;
    frame.channel = channel;
    frame.payload.resize(payload_size);
    boost::asio::buffer_copy(
        boost::asio::buffer(frame.payload),
        boost::asio::buffer(raw + kHeaderSize, static_cast<std::size_t>(payload_size))
    );

    buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(required));
    log_sync_event(
        SyncLogLevel::Trace,
        "rpc_channel",
        "decode",
        "channel=" + std::to_string(frame.channel) + " payload_bytes=" + std::to_string(frame.payload.size()) +
            " remaining_bytes=" + std::to_string(buffer.size()));
    return frame;
}

} // namespace tightrope::sync::transport
