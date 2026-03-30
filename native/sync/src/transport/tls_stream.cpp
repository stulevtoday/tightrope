#include "transport/tls_stream.h"

namespace tightrope::sync::transport {

TlsStream::TlsStream(boost::asio::io_context& io_context)
    : socket_(io_context) {}

boost::asio::ip::tcp::socket& TlsStream::socket() {
    return socket_;
}

const boost::asio::ip::tcp::socket& TlsStream::socket() const {
    return socket_;
}

bool TlsStream::is_open() const {
    return socket_.is_open();
}

void TlsStream::close() {
    if (!socket_.is_open()) {
        return;
    }
    boost::system::error_code ec;
    socket_.close(ec);
}

} // namespace tightrope::sync::transport
