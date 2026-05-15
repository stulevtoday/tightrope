#include "transport/peer_probe.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <openssl/ssl.h>

#include "sync_protocol.h"
#include "transport/rpc_channel.h"

namespace tightrope::sync::transport {

namespace {

std::uint16_t read_u16_le(const std::uint8_t* raw) {
    return static_cast<std::uint16_t>(raw[0]) | (static_cast<std::uint16_t>(raw[1]) << 8U);
}

std::uint32_t read_u32_le(const std::uint8_t* raw) {
    return static_cast<std::uint32_t>(raw[0]) | (static_cast<std::uint32_t>(raw[1]) << 8U) |
           (static_cast<std::uint32_t>(raw[2]) << 16U) | (static_cast<std::uint32_t>(raw[3]) << 24U);
}

PeerProbeResult parse_handshake_ack_payload(const std::vector<std::uint8_t>& payload) {
    if (payload.empty() || payload.front() != 1U) {
        return {
            .ok = false,
            .error = "handshake ack rejected",
        };
    }

    PeerProbeResult result{
        .ok = true,
    };
    // Backward compatible with older peers that only send {1}.
    if (payload.size() >= 5) {
        const auto remote_site_id = read_u32_le(payload.data() + 1);
        if (remote_site_id == 0) {
            return {
                .ok = false,
                .error = "handshake ack missing remote site id",
            };
        }
        result.remote_site_id = remote_site_id;
    }
    return result;
}

template <typename StartOperation, typename CancelOperation>
bool run_timed_operation(
    boost::asio::io_context& io_context,
    const std::chrono::milliseconds timeout,
    StartOperation&& start_operation,
    CancelOperation&& cancel_operation,
    std::string_view timeout_error,
    std::string* error
) {
    boost::asio::steady_timer timer(io_context);
    bool completed = false;
    bool timed_out = false;
    boost::system::error_code operation_error = boost::asio::error::would_block;

    timer.expires_after(timeout);
    timer.async_wait([&](const boost::system::error_code& ec) {
        if (ec || completed) {
            return;
        }
        timed_out = true;
        cancel_operation();
    });

    start_operation([&](const boost::system::error_code& ec) {
        operation_error = ec;
        completed = true;
        (void)timer.cancel();
    });

    io_context.restart();
    while (!completed && !timed_out) {
        if (io_context.run_one() == 0) {
            break;
        }
    }

    if (timed_out && !completed) {
        io_context.restart();
        while (!completed) {
            if (io_context.run_one() == 0) {
                break;
            }
        }
    }
    io_context.restart();

    if (timed_out) {
        if (error != nullptr) {
            *error = std::string(timeout_error);
        }
        return false;
    }
    if (!completed) {
        if (error != nullptr) {
            *error = "operation did not complete";
        }
        return false;
    }
    if (operation_error) {
        if (error != nullptr) {
            *error = operation_error.message();
        }
        return false;
    }
    return true;
}

bool connect_with_timeout(
    boost::asio::io_context& io_context,
    boost::asio::ip::tcp::socket& socket,
    const boost::asio::ip::tcp::resolver::results_type& resolved,
    const std::chrono::milliseconds timeout,
    std::string* error
) {
    return run_timed_operation(
        io_context,
        timeout,
        [&](auto done) {
            boost::asio::async_connect(
                socket,
                resolved,
                [done = std::move(done)](const boost::system::error_code& ec, const boost::asio::ip::tcp::endpoint&) mutable {
                    done(ec);
                });
        },
        [&socket]() {
            boost::system::error_code ignored;
            socket.cancel(ignored);
            socket.close(ignored);
        },
        "connect timed out",
        error);
}

bool write_tcp_with_timeout(
    boost::asio::io_context& io_context,
    boost::asio::ip::tcp::socket& socket,
    const std::vector<std::uint8_t>& payload,
    const std::chrono::milliseconds timeout,
    std::string* error
) {
    return run_timed_operation(
        io_context,
        timeout,
        [&](auto done) {
            boost::asio::async_write(
                socket,
                boost::asio::buffer(payload),
                [done = std::move(done)](const boost::system::error_code& ec, std::size_t) mutable {
                    done(ec);
                });
        },
        [&socket]() {
            boost::system::error_code ignored;
            socket.cancel(ignored);
        },
        "write timed out",
        error);
}

bool read_exact_tcp_with_timeout(
    boost::asio::io_context& io_context,
    boost::asio::ip::tcp::socket& socket,
    std::uint8_t* data,
    const std::size_t size,
    const std::chrono::milliseconds timeout,
    std::string* error
) {
    return run_timed_operation(
        io_context,
        timeout,
        [&](auto done) {
            boost::asio::async_read(
                socket,
                boost::asio::buffer(data, size),
                [done = std::move(done)](const boost::system::error_code& ec, std::size_t) mutable {
                    done(ec);
                });
        },
        [&socket]() {
            boost::system::error_code ignored;
            socket.cancel(ignored);
        },
        "read timed out",
        error);
}

bool handshake_client_with_timeout(
    boost::asio::io_context& io_context,
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& stream,
    const std::string_view server_name,
    const std::chrono::milliseconds timeout,
    std::string* error
) {
    if (!server_name.empty() && !SSL_set_tlsext_host_name(stream.native_handle(), std::string(server_name).c_str())) {
        if (error != nullptr) {
            *error = "TLS SNI host configuration failed";
        }
        return false;
    }
    return run_timed_operation(
        io_context,
        timeout,
        [&](auto done) {
            stream.async_handshake(
                boost::asio::ssl::stream_base::client,
                [done = std::move(done)](const boost::system::error_code& ec) mutable {
                    done(ec);
                });
        },
        [&stream]() {
            boost::system::error_code ignored;
            stream.lowest_layer().cancel(ignored);
            stream.lowest_layer().close(ignored);
        },
        "TLS handshake timed out",
        error);
}

bool write_tls_with_timeout(
    boost::asio::io_context& io_context,
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& stream,
    const std::vector<std::uint8_t>& payload,
    const std::chrono::milliseconds timeout,
    std::string* error
) {
    return run_timed_operation(
        io_context,
        timeout,
        [&](auto done) {
            boost::asio::async_write(
                stream,
                boost::asio::buffer(payload),
                [done = std::move(done)](const boost::system::error_code& ec, std::size_t) mutable {
                    done(ec);
                });
        },
        [&stream]() {
            boost::system::error_code ignored;
            stream.lowest_layer().cancel(ignored);
        },
        "write timed out",
        error);
}

bool read_exact_tls_with_timeout(
    boost::asio::io_context& io_context,
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& stream,
    std::uint8_t* data,
    const std::size_t size,
    const std::chrono::milliseconds timeout,
    std::string* error
) {
    return run_timed_operation(
        io_context,
        timeout,
        [&](auto done) {
            boost::asio::async_read(
                stream,
                boost::asio::buffer(data, size),
                [done = std::move(done)](const boost::system::error_code& ec, std::size_t) mutable {
                    done(ec);
                });
        },
        [&stream]() {
            boost::system::error_code ignored;
            stream.lowest_layer().cancel(ignored);
        },
        "read timed out",
        error);
}

PeerProbeResult read_handshake_ack_tcp(
    boost::asio::io_context& io_context,
    boost::asio::ip::tcp::socket& socket,
    const std::uint16_t handshake_channel,
    const std::chrono::milliseconds timeout
) {
    constexpr std::size_t kMaxAckPayloadBytes = 256;
    std::array<std::uint8_t, 6> header{};
    std::string read_error;
    if (!read_exact_tcp_with_timeout(io_context, socket, header.data(), header.size(), timeout, &read_error)) {
        return {
            .ok = false,
            .error = "failed to read handshake ack header: " + read_error,
        };
    }
    const auto channel = read_u16_le(header.data());
    const auto payload_size = static_cast<std::size_t>(read_u32_le(header.data() + 2));
    if (payload_size > kMaxAckPayloadBytes) {
        return {
            .ok = false,
            .error = "handshake ack payload too large",
        };
    }
    std::vector<std::uint8_t> payload(payload_size);
    if (payload_size > 0 &&
        !read_exact_tcp_with_timeout(io_context, socket, payload.data(), payload.size(), timeout, &read_error)) {
        return {
            .ok = false,
            .error = "failed to read handshake ack payload: " + read_error,
        };
    }

    if (channel != handshake_channel) {
        return {
            .ok = false,
            .error = "unexpected handshake ack channel " + std::to_string(channel) + " (expected " +
                     std::to_string(handshake_channel) + ")",
        };
    }
    return parse_handshake_ack_payload(payload);
}

PeerProbeResult read_handshake_ack_tls(
    boost::asio::io_context& io_context,
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& stream,
    const std::uint16_t handshake_channel,
    const std::chrono::milliseconds timeout
) {
    constexpr std::size_t kMaxAckPayloadBytes = 256;
    std::array<std::uint8_t, 6> header{};
    std::string read_error;
    if (!read_exact_tls_with_timeout(io_context, stream, header.data(), header.size(), timeout, &read_error)) {
        return {
            .ok = false,
            .error = "failed to read handshake ack header: " + read_error,
        };
    }
    const auto channel = read_u16_le(header.data());
    const auto payload_size = static_cast<std::size_t>(read_u32_le(header.data() + 2));
    if (payload_size > kMaxAckPayloadBytes) {
        return {
            .ok = false,
            .error = "handshake ack payload too large",
        };
    }
    std::vector<std::uint8_t> payload(payload_size);
    if (payload_size > 0 &&
        !read_exact_tls_with_timeout(io_context, stream, payload.data(), payload.size(), timeout, &read_error)) {
        return {
            .ok = false,
            .error = "failed to read handshake ack payload: " + read_error,
        };
    }

    if (channel != handshake_channel) {
        return {
            .ok = false,
            .error = "unexpected handshake ack channel " + std::to_string(channel) + " (expected " +
                     std::to_string(handshake_channel) + ")",
        };
    }
    return parse_handshake_ack_payload(payload);
}

} // namespace

PeerProbeResult probe_peer_handshake(const discovery::PeerEndpoint& endpoint, const PeerProbeConfig& config) {
    const auto started_at = std::chrono::steady_clock::now();
    auto elapsed_ms = [&started_at]() -> std::uint64_t {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at
        );
        if (elapsed.count() <= 0) {
            return 0;
        }
        return static_cast<std::uint64_t>(elapsed.count());
    };
    auto fail = [&elapsed_ms](std::string error) {
        return PeerProbeResult{
            .ok = false,
            .error = std::move(error),
            .duration_ms = elapsed_ms(),
        };
    };

    if (!discovery::is_valid_endpoint(endpoint)) {
        return fail("invalid peer endpoint");
    }

    boost::asio::io_context io_context;
    boost::asio::ip::tcp::resolver resolver(io_context);
    boost::system::error_code ec;
    const auto resolved = resolver.resolve(endpoint.host, std::to_string(endpoint.port), ec);
    if (ec) {
        return fail("resolve failed: " + ec.message());
    }

    sync::HandshakeFrame handshake{
        .site_id = config.local_site_id,
        .schema_version = config.local_schema_version,
        .last_recv_seq_from_peer = config.last_recv_seq_from_peer,
        .auth_key_id = config.auth_key_id,
    };
    if (config.require_handshake_auth || !config.cluster_shared_secret.empty()) {
        sync::sign_handshake(handshake, config.cluster_shared_secret);
    }
    const auto handshake_payload = sync::encode_handshake(handshake);
    const auto wire = RpcChannel::encode({
        .channel = config.handshake_channel,
        .payload = handshake_payload,
    });
    const auto timeout_ms = std::max<std::uint64_t>(config.timeout_ms, 1);
    const auto timeout = std::chrono::milliseconds(timeout_ms);

    if (!config.tls_enabled) {
        boost::asio::ip::tcp::socket socket(io_context);
        std::string operation_error;
        if (!connect_with_timeout(io_context, socket, resolved, timeout, &operation_error)) {
            return fail("connect failed: " + operation_error);
        }
        if (!write_tcp_with_timeout(io_context, socket, wire, timeout, &operation_error)) {
            return fail("write failed: " + operation_error);
        }
        auto ack = read_handshake_ack_tcp(io_context, socket, config.handshake_channel, timeout);
        ack.duration_ms = elapsed_ms();
        return ack;
    }

    TlsStream tls_stream(io_context, false);
    std::string tls_error;
    if (!tls_stream.configure(config.tls, &tls_error)) {
        return fail("tls configure failed: " + tls_error);
    }
    if (!tls_stream.set_client_hostname_verification(endpoint.host, &tls_error)) {
        return fail("tls configure failed: " + tls_error);
    }
    std::string operation_error;
    if (!connect_with_timeout(io_context, tls_stream.socket(), resolved, timeout, &operation_error)) {
        return fail("connect failed: " + operation_error);
    }
    if (!handshake_client_with_timeout(io_context, tls_stream.stream(), endpoint.host, timeout, &tls_error)) {
        return fail("tls handshake failed: " + tls_error);
    }
    if (!write_tls_with_timeout(io_context, tls_stream.stream(), wire, timeout, &operation_error)) {
        return fail("write failed: " + operation_error);
    }
    auto ack = read_handshake_ack_tls(io_context, tls_stream.stream(), config.handshake_channel, timeout);
    ack.duration_ms = elapsed_ms();
    return ack;
}

} // namespace tightrope::sync::transport
