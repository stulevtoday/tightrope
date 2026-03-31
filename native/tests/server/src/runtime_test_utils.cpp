#include "server/runtime_test_utils.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>

namespace tightrope::tests::server {

std::uint16_t next_runtime_port() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
        sockaddr_in address{};
        std::memset(&address, 0, sizeof(address));
#if defined(__APPLE__)
        address.sin_len = static_cast<__uint8_t>(sizeof(address));
#endif
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;

        if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
            sockaddr_in bound{};
            socklen_t bound_len = sizeof(bound);
            if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &bound_len) == 0) {
                const auto selected_port = static_cast<std::uint16_t>(ntohs(bound.sin_port));
                ::close(fd);
                if (selected_port != 0) {
                    return selected_port;
                }
            }
        }
        ::close(fd);
    }

    // Fallback for environments where ephemeral bind probing is unavailable.
    const auto fallback_seed = static_cast<std::uint16_t>(
        40000 + (static_cast<std::uint16_t>(::getpid()) % static_cast<std::uint16_t>(20000))
    );
    static std::atomic<std::uint16_t> fallback_port{fallback_seed};
    return static_cast<std::uint16_t>(fallback_port.fetch_add(17));
}

std::string make_temp_runtime_db_path() {
    static std::atomic<std::uint32_t> suffix{1};
    const auto filename = "tightrope-runtime-" + std::to_string(suffix.fetch_add(1)) + ".sqlite3";
    const auto path = std::filesystem::temp_directory_path() / std::filesystem::path(filename);
    std::filesystem::remove(path);
    return path.string();
}

EnvVarGuard::EnvVarGuard(const char* key) : key_(key) {
    if (const char* existing = std::getenv(key_.c_str()); existing != nullptr) {
        original_ = std::string(existing);
    }
}

EnvVarGuard::~EnvVarGuard() {
    if (original_.has_value()) {
        (void)::setenv(key_.c_str(), original_->c_str(), 1);
    } else {
        (void)::unsetenv(key_.c_str());
    }
}

bool EnvVarGuard::set(const std::string_view value) const {
    const std::string copy(value);
    return ::setenv(key_.c_str(), copy.c_str(), 1) == 0;
}

std::string send_raw_http_to_host(
    const std::string_view host,
    const std::uint16_t port,
    const std::string_view request,
    const int max_reads
) {
    addrinfo hints{};
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    const std::string host_copy(host);
    const std::string service = std::to_string(port);
    addrinfo* results = nullptr;
    if (::getaddrinfo(host_copy.c_str(), service.c_str(), &hints, &results) != 0 || results == nullptr) {
        return {};
    }

    int fd = -1;
    for (addrinfo* it = results; it != nullptr; it = it->ai_next) {
        fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (::connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
            break;
        }
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(results);

    if (fd < 0) {
        return {};
    }

    timeval timeout{};
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
        ::close(fd);
        return {};
    }

    std::size_t sent_total = 0;
    while (sent_total < request.size()) {
        const auto sent = ::send(fd, request.data() + sent_total, request.size() - sent_total, 0);
        if (sent < 0) {
            ::close(fd);
            return {};
        }
        sent_total += static_cast<std::size_t>(sent);
    }

    std::string response;
    char buffer[4096];
    for (int index = 0; index < max_reads; ++index) {
        const auto read = ::recv(fd, buffer, sizeof(buffer), 0);
        if (read <= 0) {
            break;
        }
        response.append(buffer, static_cast<std::size_t>(read));
    }

    ::close(fd);
    return response;
}

std::string send_raw_http(const std::uint16_t port, const std::string_view request, const int max_reads) {
    return send_raw_http_to_host("127.0.0.1", port, request, max_reads);
}

std::string http_body(const std::string_view http_response) {
    const auto separator = http_response.find("\r\n\r\n");
    if (separator == std::string_view::npos) {
        return std::string(http_response);
    }
    return std::string(http_response.substr(separator + 4));
}

std::optional<std::string> http_header_value(
    const std::string_view http_response,
    const std::string_view header_name
) {
    const std::string needle = std::string(header_name) + ":";
    std::size_t cursor = 0;
    while (cursor < http_response.size()) {
        const auto line_end = http_response.find("\r\n", cursor);
        const auto end = line_end == std::string_view::npos ? http_response.size() : line_end;
        const auto line = http_response.substr(cursor, end - cursor);
        if (line.empty()) {
            break;
        }
        if (line.size() > needle.size() && line.substr(0, needle.size()) == needle) {
            std::string value(line.substr(needle.size()));
            while (!value.empty() && value.front() == ' ') {
                value.erase(value.begin());
            }
            return value;
        }
        if (line_end == std::string_view::npos) {
            break;
        }
        cursor = line_end + 2;
    }
    return std::nullopt;
}

} // namespace tightrope::tests::server
