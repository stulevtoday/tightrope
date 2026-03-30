#pragma once
// TLS 1.3 TCP connection

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace tightrope::sync::transport {

class TlsStream {
public:
    explicit TlsStream(boost::asio::io_context& io_context);

    boost::asio::ip::tcp::socket& socket();
    const boost::asio::ip::tcp::socket& socket() const;

    bool is_open() const;
    void close();

private:
    boost::asio::ip::tcp::socket socket_;
};

} // namespace tightrope::sync::transport
