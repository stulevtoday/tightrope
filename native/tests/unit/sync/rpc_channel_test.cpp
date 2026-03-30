#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "transport/rpc_channel.h"

TEST_CASE("rpc channel encodes and decodes frames", "[sync][transport][rpc]") {
    const tightrope::sync::transport::RpcFrame frame = {
        .channel = 3,
        .payload = std::vector<std::uint8_t>{'o', 'k'},
    };

    auto bytes = tightrope::sync::transport::RpcChannel::encode(frame);
    const auto decoded = tightrope::sync::transport::RpcChannel::try_decode(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->channel == frame.channel);
    REQUIRE(decoded->payload == frame.payload);
    REQUIRE(bytes.empty());
}

TEST_CASE("rpc channel keeps partial frame buffered", "[sync][transport][rpc]") {
    const tightrope::sync::transport::RpcFrame frame = {
        .channel = 9,
        .payload = std::vector<std::uint8_t>{'x', 'y', 'z'},
    };
    auto bytes = tightrope::sync::transport::RpcChannel::encode(frame);
    bytes.pop_back();

    const auto decoded = tightrope::sync::transport::RpcChannel::try_decode(bytes);
    REQUIRE_FALSE(decoded.has_value());
    REQUIRE_FALSE(bytes.empty());
}

TEST_CASE("rpc channel encodes in little-endian byte order", "[sync][transport][rpc]") {
    const tightrope::sync::transport::RpcFrame frame = {
        .channel = 0x0102,
        .payload = std::vector<std::uint8_t>{'A'},
    };

    const auto bytes = tightrope::sync::transport::RpcChannel::encode(frame);
    REQUIRE(bytes.size() == 7);

    REQUIRE(bytes[0] == 0x02);
    REQUIRE(bytes[1] == 0x01);

    REQUIRE(bytes[2] == 0x01);
    REQUIRE(bytes[3] == 0x00);
    REQUIRE(bytes[4] == 0x00);
    REQUIRE(bytes[5] == 0x00);

    REQUIRE(bytes[6] == 'A');
}

TEST_CASE("rpc channel encodes multi-byte payload size in little-endian", "[sync][transport][rpc]") {
    std::vector<std::uint8_t> payload(258, 0x42); // 258 => 0x00000102
    const tightrope::sync::transport::RpcFrame frame = {
        .channel = 1,
        .payload = payload,
    };

    const auto bytes = tightrope::sync::transport::RpcChannel::encode(frame);
    REQUIRE(bytes[2] == 0x02);
    REQUIRE(bytes[3] == 0x01);
    REQUIRE(bytes[4] == 0x00);
    REQUIRE(bytes[5] == 0x00);
}
