#pragma once
// TLS 1.3 TCP connection

#include <string>
#include <string_view>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>

namespace tightrope::sync::transport {

struct TlsConfig {
    std::string ca_certificate_path;
    std::string certificate_chain_path;
    std::string private_key_path;
    std::string pinned_peer_certificate_sha256;
    bool verify_peer = true;
};

class TlsStream {
public:
    explicit TlsStream(boost::asio::io_context& io_context, bool server_mode = false);

    boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& stream();
    const boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& stream() const;
    boost::asio::ip::tcp::socket& socket();
    const boost::asio::ip::tcp::socket& socket() const;

    bool configure(const TlsConfig& config, std::string* error = nullptr);
    bool set_client_hostname_verification(std::string_view server_name, std::string* error = nullptr);
    bool handshake_client(std::string_view server_name, std::string* error = nullptr);
    bool handshake_server(std::string* error = nullptr);

    bool is_open() const;
    void close();

private:
    bool configure_verify_callback();
    bool verify_preverified_and_pin(bool preverified, boost::asio::ssl::verify_context& context) const;
    static std::string normalize_fingerprint(std::string_view fingerprint);

    boost::asio::ssl::context context_;
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> stream_;
    bool server_mode_ = false;
    bool verify_peer_ = false;
    std::string pinned_peer_certificate_sha256_;
};

} // namespace tightrope::sync::transport
