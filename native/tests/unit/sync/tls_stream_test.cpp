#include <catch2/catch_test_macros.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <openssl/ssl.h>
#include <string>

#include "transport/tls_stream.h"

TEST_CASE("tls stream wraps asio socket lifecycle", "[sync][transport][tls]") {
    boost::asio::io_context io;
    tightrope::sync::transport::TlsStream stream(io);

    REQUIRE_FALSE(stream.is_open());
    stream.socket().open(boost::asio::ip::tcp::v4());
    REQUIRE(stream.is_open());

    stream.close();
    REQUIRE_FALSE(stream.is_open());
}

TEST_CASE("tls stream configure toggles peer verification mode", "[sync][transport][tls]") {
    boost::asio::io_context io;
    tightrope::sync::transport::TlsStream stream(io);

    tightrope::sync::transport::TlsConfig insecure{};
    insecure.verify_peer = false;
    std::string error;
    REQUIRE(stream.configure(insecure, &error));
    REQUIRE((SSL_get_verify_mode(stream.stream().native_handle()) & SSL_VERIFY_PEER) == 0);

    tightrope::sync::transport::TlsConfig secure{};
    secure.verify_peer = true;
    REQUIRE(stream.configure(secure, &error));
    REQUIRE((SSL_get_verify_mode(stream.stream().native_handle()) & SSL_VERIFY_PEER) != 0);
    REQUIRE(stream.set_client_hostname_verification("example.com", &error));
}

TEST_CASE("tls stream rejects invalid pinned certificate fingerprint", "[sync][transport][tls]") {
    boost::asio::io_context io;
    tightrope::sync::transport::TlsStream stream(io);

    tightrope::sync::transport::TlsConfig config{};
    config.verify_peer = false;
    config.pinned_peer_certificate_sha256 = "invalid-fingerprint";
    std::string error;
    REQUIRE_FALSE(stream.configure(config, &error));
    REQUIRE(error.find("64-character SHA-256 hex") != std::string::npos);
}
