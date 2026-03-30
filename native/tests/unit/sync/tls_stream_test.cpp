#include <catch2/catch_test_macros.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

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
