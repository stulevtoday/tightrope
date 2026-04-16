#include <catch2/catch_test_macros.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <optional>
#include <cctype>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>
#if TIGHTROPE_HAS_ZSTD_DECOMPRESSION
#include <zstd.h>
#endif

#include "controllers/proxy_controller.h"
#include "upstream_transport.h"

namespace {

class EnvVarGuard final {
public:
    explicit EnvVarGuard(std::string name) : name_(std::move(name)) {
        if (const char* existing = std::getenv(name_.c_str()); existing != nullptr) {
            had_original_ = true;
            original_ = existing;
        }
    }

    ~EnvVarGuard() {
        if (had_original_) {
            setenv(name_.c_str(), original_.c_str(), 1);
            return;
        }
        unsetenv(name_.c_str());
    }

private:
    std::string name_;
    bool had_original_ = false;
    std::string original_;
};

std::string lower_ascii(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::optional<std::string> request_header_value(const std::string& raw_request, const std::string_view name) {
    const auto header_end = raw_request.find("\r\n\r\n");
    const auto headers_only = raw_request.substr(0, header_end);
    const auto needle = lower_ascii(std::string(name)) + ":";
    std::size_t start = 0;
    while (start < headers_only.size()) {
        const auto end = headers_only.find("\r\n", start);
        const auto line = headers_only.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const auto lower_line = lower_ascii(line);
        if (lower_line.rfind(needle, 0) == 0) {
            const auto value = line.substr(needle.size());
            std::size_t value_start = 0;
            while (value_start < value.size() && std::isspace(static_cast<unsigned char>(value[value_start]))) {
                ++value_start;
            }
            return value.substr(value_start);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 2;
    }
    return std::nullopt;
}

std::string request_body(const std::string& raw_request) {
    const auto header_end = raw_request.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return {};
    }
    return raw_request.substr(header_end + 4);
}

class SingleRequestHttpServer final {
public:
    SingleRequestHttpServer(std::string status_line, std::string content_type, std::string body, std::size_t request_limit = 1)
        : status_line_(std::move(status_line)),
          content_type_(std::move(content_type)),
          body_(std::move(body)),
          request_limit_(request_limit) {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(listen_fd_ >= 0);

        int opt = 1;
        REQUIRE(::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == 0);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(0);
        REQUIRE(::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(::listen(listen_fd_, 1) == 0);

        socklen_t len = sizeof(addr);
        REQUIRE(::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
        port_ = ntohs(addr.sin_port);
        REQUIRE(port_ > 0);

        thread_ = std::thread([this] { serve_loop(); });
    }

    ~SingleRequestHttpServer() {
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    [[nodiscard]] std::uint16_t port() const {
        return port_;
    }

    [[nodiscard]] std::string captured_request() const {
        if (captured_requests_.empty()) {
            return {};
        }
        return captured_requests_.back();
    }

private:
    void serve_loop() {
        std::size_t served = 0;
        while (served < request_limit_) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            const int client_fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client_fd < 0) {
                break;
            }
            serve_client(client_fd);
            served += 1;
        }
    }

    void serve_client(const int client_fd) {
        std::string request;
        char buffer[4096];
        while (true) {
            const auto read = ::recv(client_fd, buffer, sizeof(buffer), 0);
            if (read <= 0) {
                break;
            }
            request.append(buffer, static_cast<std::size_t>(read));
            const auto header_end = request.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                auto content_length = std::size_t{0};
                const auto header_lines = request.substr(0, header_end);
                const auto key = std::string("Content-Length:");
                const auto idx = header_lines.find(key);
                if (idx != std::string::npos) {
                    const auto value_start = idx + key.size();
                    const auto value_end = header_lines.find("\r\n", value_start);
                    const auto value = header_lines.substr(value_start, value_end - value_start);
                    content_length = static_cast<std::size_t>(std::strtoul(value.c_str(), nullptr, 10));
                }
                const auto body_size = request.size() - (header_end + 4);
                if (body_size >= content_length) {
                    break;
                }
            }
        }
        captured_requests_.push_back(request);

        const auto response = "HTTP/1.1 " + status_line_ + "\r\nContent-Type: " + content_type_ +
                              "\r\nContent-Length: " + std::to_string(body_.size()) + "\r\nConnection: close\r\n\r\n" +
                              body_;
        const auto* data = response.data();
        std::size_t sent_total = 0;
        while (sent_total < response.size()) {
            const auto sent = ::send(client_fd, data + sent_total, response.size() - sent_total, 0);
            if (sent <= 0) {
                break;
            }
            sent_total += static_cast<std::size_t>(sent);
        }

        ::shutdown(client_fd, SHUT_RDWR);
        ::close(client_fd);
    }

    int listen_fd_ = -1;
    std::thread thread_;
    std::uint16_t port_ = 0;
    std::string status_line_;
    std::string content_type_;
    std::string body_;
    std::size_t request_limit_ = 1;
    std::vector<std::string> captured_requests_;
};

class SingleRequestWebSocketServer final {
public:
    SingleRequestWebSocketServer(std::vector<std::string> response_frames)
        : response_frames_(std::move(response_frames)) {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(listen_fd_ >= 0);

        int opt = 1;
        REQUIRE(::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == 0);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(0);
        REQUIRE(::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(::listen(listen_fd_, 1) == 0);

        socklen_t len = sizeof(addr);
        REQUIRE(::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
        port_ = ntohs(addr.sin_port);
        REQUIRE(port_ > 0);

        thread_ = std::thread([this] { serve_once(); });
    }

    ~SingleRequestWebSocketServer() {
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    [[nodiscard]] std::uint16_t port() const {
        return port_;
    }

    [[nodiscard]] std::string captured_payload() const {
        return captured_payload_;
    }

private:
    void serve_once() {
        namespace beast = boost::beast;
        namespace websocket = beast::websocket;
        namespace asio = boost::asio;
        using tcp = asio::ip::tcp;

        const int client_fd = ::accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            return;
        }

        asio::io_context io_context;
        beast::tcp_stream stream(io_context);
        boost::system::error_code ec;
        stream.socket().assign(tcp::v4(), client_fd, ec);
        if (ec) {
            ::close(client_fd);
            return;
        }

        websocket::stream<beast::tcp_stream> ws(std::move(stream));
        ws.accept(ec);
        if (ec) {
            return;
        }

        beast::flat_buffer read_buffer;
        ws.read(read_buffer, ec);
        if (!ec) {
            captured_payload_ = beast::buffers_to_string(read_buffer.data());
            for (const auto& frame : response_frames_) {
                ws.text(true);
                ws.write(asio::buffer(frame), ec);
                if (ec) {
                    break;
                }
            }
        }

        boost::system::error_code close_ec;
        beast::get_lowest_layer(ws).socket().shutdown(tcp::socket::shutdown_both, close_ec);
        beast::get_lowest_layer(ws).socket().close(close_ec);
    }

    int listen_fd_ = -1;
    std::thread thread_;
    std::uint16_t port_ = 0;
    std::vector<std::string> response_frames_;
    std::string captured_payload_;
};

class ContinuityAwareWebSocketServer final {
public:
    ContinuityAwareWebSocketServer() {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(listen_fd_ >= 0);

        int opt = 1;
        REQUIRE(::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == 0);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(0);
        REQUIRE(::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(::listen(listen_fd_, 4) == 0);

        socklen_t len = sizeof(addr);
        REQUIRE(::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
        port_ = ntohs(addr.sin_port);
        REQUIRE(port_ > 0);

        thread_ = std::thread([this] { serve_loop(); });
    }

    ~ContinuityAwareWebSocketServer() {
        stopping_.store(true);
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    [[nodiscard]] std::uint16_t port() const {
        return port_;
    }

    [[nodiscard]] int connection_count() const {
        return connection_count_.load();
    }

    [[nodiscard]] int request_count() const {
        return request_count_.load();
    }

private:
    void serve_loop() {
        while (!stopping_.load()) {
            if (request_count_.load() >= 2) {
                break;
            }
            const int client_fd = ::accept(listen_fd_, nullptr, nullptr);
            if (client_fd < 0) {
                if (stopping_.load()) {
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            serve_connection(client_fd);
        }
    }

    void serve_connection(const int client_fd) {
        namespace asio = boost::asio;
        namespace beast = boost::beast;
        namespace websocket = beast::websocket;
        using tcp = asio::ip::tcp;

        connection_count_.fetch_add(1);

        asio::io_context io_context;
        beast::tcp_stream stream(io_context);
        boost::system::error_code ec;
        stream.socket().assign(tcp::v4(), client_fd, ec);
        if (ec) {
            ::close(client_fd);
            return;
        }

        websocket::stream<beast::tcp_stream> ws(std::move(stream));
        ws.accept(ec);
        if (ec) {
            return;
        }

        int local_request_count = 0;
        while (!stopping_.load() && request_count_.load() < 2) {
            beast::flat_buffer read_buffer;
            ws.read(read_buffer, ec);
            if (ec == websocket::error::closed) {
                break;
            }
            if (ec) {
                break;
            }

            const auto payload = beast::buffers_to_string(read_buffer.data());
            local_request_count += 1;
            request_count_.fetch_add(1);
            const bool has_previous = payload.find("\"previous_response_id\":\"resp_ws_bridge_1\"") != std::string::npos;

            if (local_request_count == 1 && has_previous) {
                const std::string failed =
                    R"({"type":"response.failed","response":{"id":"resp_ws_bridge_failed","status":"failed"},"error":{"type":"invalid_request_error","code":"previous_response_not_found","message":"Previous response not found.","param":"previous_response_id"}})";
                ws.text(true);
                ws.write(asio::buffer(failed), ec);
                break;
            }

            const std::string response_id = local_request_count == 1 ? "resp_ws_bridge_1" : "resp_ws_bridge_2";
            const std::string created = std::string(R"({"type":"response.created","response":{"id":")") + response_id +
                                        R"(","status":"in_progress"}})";
            const std::string completed = std::string(R"({"type":"response.completed","response":{"id":")") +
                                          response_id + R"(","object":"response","status":"completed","output":[]}})";

            ws.text(true);
            ws.write(asio::buffer(created), ec);
            if (ec) {
                break;
            }
            ws.text(true);
            ws.write(asio::buffer(completed), ec);
            if (ec) {
                break;
            }
        }

        boost::system::error_code close_ec;
        ws.close(websocket::close_code::normal, close_ec);
    }

    int listen_fd_ = -1;
    std::thread thread_;
    std::uint16_t port_ = 0;
    std::atomic<bool> stopping_{false};
    std::atomic<int> connection_count_{0};
    std::atomic<int> request_count_{0};
};

class TimeoutThenRecoverWebSocketServer final {
public:
    TimeoutThenRecoverWebSocketServer() {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(listen_fd_ >= 0);

        int opt = 1;
        REQUIRE(::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == 0);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(0);
        REQUIRE(::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(::listen(listen_fd_, 4) == 0);

        socklen_t len = sizeof(addr);
        REQUIRE(::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
        port_ = ntohs(addr.sin_port);
        REQUIRE(port_ > 0);

        thread_ = std::thread([this] { serve_loop(); });
    }

    ~TimeoutThenRecoverWebSocketServer() {
        stopping_.store(true);
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    [[nodiscard]] std::uint16_t port() const {
        return port_;
    }

    [[nodiscard]] int connection_count() const {
        return connection_count_.load();
    }

    [[nodiscard]] int request_count() const {
        return request_count_.load();
    }

private:
    void serve_loop() {
        while (!stopping_.load() && request_count_.load() < 2) {
            const int client_fd = ::accept(listen_fd_, nullptr, nullptr);
            if (client_fd < 0) {
                if (stopping_.load()) {
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            serve_connection(client_fd);
        }
    }

    void serve_connection(const int client_fd) {
        namespace asio = boost::asio;
        namespace beast = boost::beast;
        namespace websocket = beast::websocket;
        using tcp = asio::ip::tcp;

        connection_count_.fetch_add(1);

        asio::io_context io_context;
        beast::tcp_stream stream(io_context);
        boost::system::error_code ec;
        stream.socket().assign(tcp::v4(), client_fd, ec);
        if (ec) {
            ::close(client_fd);
            return;
        }

        websocket::stream<beast::tcp_stream> ws(std::move(stream));
        ws.accept(ec);
        if (ec) {
            return;
        }

        beast::flat_buffer read_buffer;
        ws.read(read_buffer, ec);
        if (ec) {
            return;
        }

        const auto request_index = request_count_.fetch_add(1) + 1;
        if (request_index == 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            boost::system::error_code close_ec;
            ws.close(websocket::close_code::normal, close_ec);
            return;
        }

        const std::string created =
            R"({"type":"response.created","response":{"id":"resp_ws_timeout_recovered","status":"in_progress"}})";
        const std::string completed =
            R"({"type":"response.completed","response":{"id":"resp_ws_timeout_recovered","object":"response","status":"completed","output":[]}})";

        ws.text(true);
        ws.write(asio::buffer(created), ec);
        if (ec) {
            return;
        }
        ws.text(true);
        ws.write(asio::buffer(completed), ec);
        if (ec) {
            return;
        }

        boost::system::error_code close_ec;
        ws.close(websocket::close_code::normal, close_ec);
    }

    int listen_fd_ = -1;
    std::thread thread_;
    std::uint16_t port_ = 0;
    std::atomic<bool> stopping_{false};
    std::atomic<int> connection_count_{0};
    std::atomic<int> request_count_{0};
};

class CloseAfterFirstTurnWebSocketServer final {
public:
    CloseAfterFirstTurnWebSocketServer() {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(listen_fd_ >= 0);

        int opt = 1;
        REQUIRE(::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == 0);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(0);
        REQUIRE(::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(::listen(listen_fd_, 4) == 0);

        socklen_t len = sizeof(addr);
        REQUIRE(::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
        port_ = ntohs(addr.sin_port);
        REQUIRE(port_ > 0);

        thread_ = std::thread([this] { serve_loop(); });
    }

    ~CloseAfterFirstTurnWebSocketServer() {
        stopping_.store(true);
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    [[nodiscard]] std::uint16_t port() const {
        return port_;
    }

    [[nodiscard]] int connection_count() const {
        return connection_count_.load();
    }

    [[nodiscard]] int request_count() const {
        return request_count_.load();
    }

private:
    void serve_loop() {
        while (!stopping_.load() && request_count_.load() < 2) {
            const int client_fd = ::accept(listen_fd_, nullptr, nullptr);
            if (client_fd < 0) {
                if (stopping_.load()) {
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            serve_connection(client_fd);
        }
    }

    void serve_connection(const int client_fd) {
        namespace asio = boost::asio;
        namespace beast = boost::beast;
        namespace websocket = beast::websocket;
        using tcp = asio::ip::tcp;

        connection_count_.fetch_add(1);

        asio::io_context io_context;
        beast::tcp_stream stream(io_context);
        boost::system::error_code ec;
        stream.socket().assign(tcp::v4(), client_fd, ec);
        if (ec) {
            ::close(client_fd);
            return;
        }

        websocket::stream<beast::tcp_stream> ws(std::move(stream));
        ws.accept(ec);
        if (ec) {
            return;
        }

        beast::flat_buffer read_buffer;
        ws.read(read_buffer, ec);
        if (ec) {
            return;
        }

        const auto request_index = request_count_.fetch_add(1) + 1;
        const auto response_id = request_index == 1 ? "resp_ws_stale_first" : "resp_ws_stale_second";
        const std::string created = std::string(R"({"type":"response.created","response":{"id":")") + response_id +
                                    R"(","status":"in_progress"}})";
        const std::string completed = std::string(R"({"type":"response.completed","response":{"id":")") + response_id +
                                      R"(","object":"response","status":"completed","output":[]}})";

        ws.text(true);
        ws.write(asio::buffer(created), ec);
        if (ec) {
            return;
        }
        ws.text(true);
        ws.write(asio::buffer(completed), ec);
        if (ec) {
            return;
        }

        // Always close the connection after serving a turn. The second request must
        // recover by reconnecting when the first upstream bridge socket becomes stale.
        boost::system::error_code close_ec;
        ws.close(websocket::close_code::normal, close_ec);
    }

    int listen_fd_ = -1;
    std::thread thread_;
    std::uint16_t port_ = 0;
    std::atomic<bool> stopping_{false};
    std::atomic<int> connection_count_{0};
    std::atomic<int> request_count_{0};
};

class ParallelSessionWebSocketServer final {
public:
    ParallelSessionWebSocketServer() {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(listen_fd_ >= 0);

        int opt = 1;
        REQUIRE(::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == 0);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(0);
        REQUIRE(::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(::listen(listen_fd_, 8) == 0);

        socklen_t len = sizeof(addr);
        REQUIRE(::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
        port_ = ntohs(addr.sin_port);
        REQUIRE(port_ > 0);

        thread_ = std::thread([this] { serve_loop(); });
    }

    ~ParallelSessionWebSocketServer() {
        stopping_.store(true);
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    [[nodiscard]] std::uint16_t port() const {
        return port_;
    }

    [[nodiscard]] int connection_count() const {
        return connection_count_.load();
    }

    [[nodiscard]] int request_count() const {
        return request_count_.load();
    }

private:
    void serve_loop() {
        while (!stopping_.load() && connection_count_.load() < 2) {
            const int client_fd = ::accept(listen_fd_, nullptr, nullptr);
            if (client_fd < 0) {
                if (stopping_.load()) {
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            workers_.emplace_back([this, client_fd] { serve_connection(client_fd); });
        }

        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    void serve_connection(const int client_fd) {
        namespace asio = boost::asio;
        namespace beast = boost::beast;
        namespace websocket = beast::websocket;
        using tcp = asio::ip::tcp;

        connection_count_.fetch_add(1);

        asio::io_context io_context;
        beast::tcp_stream stream(io_context);
        boost::system::error_code ec;
        stream.socket().assign(tcp::v4(), client_fd, ec);
        if (ec) {
            ::close(client_fd);
            return;
        }

        websocket::stream<beast::tcp_stream> ws(std::move(stream));
        ws.accept(ec);
        if (ec) {
            return;
        }

        beast::flat_buffer read_buffer;
        ws.read(read_buffer, ec);
        if (ec) {
            return;
        }

        const auto request_index = request_count_.fetch_add(1) + 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const auto response_id = std::string("resp_parallel_") + std::to_string(request_index);

        const std::string created =
            std::string(R"({"type":"response.created","response":{"id":")") + response_id +
            R"(","status":"in_progress"}})";
        const std::string completed =
            std::string(R"({"type":"response.completed","response":{"id":")") + response_id +
            R"(","object":"response","status":"completed","output":[]}})";

        ws.text(true);
        ws.write(asio::buffer(created), ec);
        if (ec) {
            return;
        }
        ws.text(true);
        ws.write(asio::buffer(completed), ec);

        boost::system::error_code close_ec;
        ws.close(websocket::close_code::normal, close_ec);
    }

    int listen_fd_ = -1;
    std::thread thread_;
    std::vector<std::thread> workers_;
    std::uint16_t port_ = 0;
    std::atomic<bool> stopping_{false};
    std::atomic<int> connection_count_{0};
    std::atomic<int> request_count_{0};
};

} // namespace

TEST_CASE("default upstream transport executes real HTTP responses request", "[proxy][transport][default]") {
    tightrope::proxy::reset_upstream_transport();

    SingleRequestHttpServer server{
        "200 OK",
        "application/json",
        R"({"id":"resp_default_http","object":"response","status":"completed","output":[]})",
    };

    EnvVarGuard base_url_guard("TIGHTROPE_UPSTREAM_BASE_URL");
    const auto base_url = std::string("http://127.0.0.1:") + std::to_string(server.port());
    REQUIRE(setenv("TIGHTROPE_UPSTREAM_BASE_URL", base_url.c_str(), 1) == 0);

    const auto response = tightrope::server::controllers::post_proxy_responses_json(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"default-http"})"
    );

    REQUIRE(response.status == 200);
    REQUIRE(response.body.find("\"resp_default_http\"") != std::string::npos);
    REQUIRE(server.captured_request().find("POST /codex/responses HTTP/1.1") != std::string::npos);
}

TEST_CASE("default upstream transport executes compact path over HTTP", "[proxy][transport][default]") {
    tightrope::proxy::reset_upstream_transport();

    SingleRequestHttpServer server{
        "200 OK",
        "application/json",
        R"({"object":"response.compact","id":"resp_compact_default","status":"completed","output":[]})",
    };

    EnvVarGuard base_url_guard("TIGHTROPE_UPSTREAM_BASE_URL");
    const auto base_url = std::string("http://127.0.0.1:") + std::to_string(server.port());
    REQUIRE(setenv("TIGHTROPE_UPSTREAM_BASE_URL", base_url.c_str(), 1) == 0);

    const auto response = tightrope::server::controllers::post_proxy_responses_compact(
        "/v1/responses/compact",
        R"({"model":"gpt-5.4","input":"compact-default"})"
    );

    REQUIRE(response.status == 200);
    REQUIRE(response.body.find("\"resp_compact_default\"") != std::string::npos);
    REQUIRE(server.captured_request().find("POST /codex/responses/compact HTTP/1.1") != std::string::npos);
}

TEST_CASE("default upstream transport optionally applies zstd request compression", "[proxy][transport][default]") {
    tightrope::proxy::reset_upstream_transport();

    SingleRequestHttpServer server{
        "200 OK",
        "application/json",
        R"({"id":"resp_default_compressed","object":"response","status":"completed","output":[]})",
    };

    EnvVarGuard base_url_guard("TIGHTROPE_UPSTREAM_BASE_URL");
    EnvVarGuard compression_guard("TIGHTROPE_UPSTREAM_ENABLE_REQUEST_COMPRESSION");
    const auto base_url = std::string("http://127.0.0.1:") + std::to_string(server.port());
    REQUIRE(setenv("TIGHTROPE_UPSTREAM_BASE_URL", base_url.c_str(), 1) == 0);
    REQUIRE(setenv("TIGHTROPE_UPSTREAM_ENABLE_REQUEST_COMPRESSION", "true", 1) == 0);

    const auto response = tightrope::server::controllers::post_proxy_responses_json(
        "/backend-api/codex/responses",
        R"({"model":"gpt-5.4","input":"compress-me"})"
    );

    REQUIRE(response.status == 200);
    const auto captured = server.captured_request();
    REQUIRE(captured.find("POST /codex/responses HTTP/1.1") != std::string::npos);

#if TIGHTROPE_HAS_ZSTD_DECOMPRESSION
    REQUIRE(request_header_value(captured, "Content-Encoding").value_or("") == "zstd");
    const auto compressed_body = request_body(captured);
    REQUIRE_FALSE(compressed_body.empty());

    const auto expected_upper_bound = ZSTD_getFrameContentSize(compressed_body.data(), compressed_body.size());
    REQUIRE(expected_upper_bound != ZSTD_CONTENTSIZE_ERROR);
    REQUIRE(expected_upper_bound != ZSTD_CONTENTSIZE_UNKNOWN);
    std::string decompressed(expected_upper_bound, '\0');
    const auto actual_size = ZSTD_decompress(
        decompressed.data(),
        decompressed.size(),
        compressed_body.data(),
        compressed_body.size()
    );
    REQUIRE(ZSTD_isError(actual_size) == 0);
    decompressed.resize(actual_size);
    REQUIRE(decompressed.find("\"model\":\"gpt-5.4\"") != std::string::npos);
    REQUIRE(decompressed.find("\"input\"") != std::string::npos);
#else
    REQUIRE(request_header_value(captured, "Content-Encoding").has_value() == false);
    REQUIRE(captured.find("\"model\":\"gpt-5.4\"") != std::string::npos);
#endif
}

TEST_CASE("default upstream transport does not downgrade websocket transport to HTTP fallback", "[proxy][transport][default]") {
    tightrope::proxy::reset_upstream_transport();

    const std::string sse_body =
        "data: {\"type\":\"response.text.delta\",\"delta\":\"hello\"}\n\n"
        "data: {\"type\":\"response.completed\",\"response\":{\"id\":\"resp_ws_default\",\"object\":\"response\",\"status\":"
        "\"completed\",\"output\":[]}}\n\n"
        "data: [DONE]\n\n";

    SingleRequestHttpServer server{
        "200 OK",
        "text/event-stream",
        sse_body,
        2,
    };

    EnvVarGuard base_url_guard("TIGHTROPE_UPSTREAM_BASE_URL");
    const auto base_url = std::string("http://127.0.0.1:") + std::to_string(server.port());
    REQUIRE(setenv("TIGHTROPE_UPSTREAM_BASE_URL", base_url.c_str(), 1) == 0);

    const tightrope::proxy::openai::HeaderMap ws_headers = {
        {"Connection", "Upgrade"},
        {"Sec-WebSocket-Key", "abc"},
        {"Originator", "codex_cli_rs"},
    };
    const auto response = tightrope::server::controllers::proxy_responses_websocket(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"ws-fallback"})",
        ws_headers
    );

    REQUIRE(response.status == 502);
    REQUIRE_FALSE(response.accepted);
    REQUIRE(response.close_code == 1011);
    REQUIRE_FALSE(response.frames.empty());
    REQUIRE(server.captured_request().find("/codex/responses") != std::string::npos);
}

TEST_CASE("default upstream transport executes real websocket upstream exchange", "[proxy][transport][default]") {
    tightrope::proxy::reset_upstream_transport();

    SingleRequestWebSocketServer server({
        R"({"type":"response.output_text.delta","delta":"hello"})",
        R"({"type":"response.completed","response":{"id":"resp_ws_real","object":"response","status":"completed","output":[]}})",
    });

    EnvVarGuard base_url_guard("TIGHTROPE_UPSTREAM_BASE_URL");
    const auto base_url = std::string("http://127.0.0.1:") + std::to_string(server.port());
    REQUIRE(setenv("TIGHTROPE_UPSTREAM_BASE_URL", base_url.c_str(), 1) == 0);

    const tightrope::proxy::openai::HeaderMap ws_headers = {
        {"Connection", "Upgrade"},
        {"Sec-WebSocket-Key", "abc"},
        {"Originator", "codex_cli_rs"},
    };
    const auto response = tightrope::server::controllers::proxy_responses_websocket(
        "/v1/responses",
        R"({"model":"gpt-5.4","input":"ws-real"})",
        ws_headers
    );

    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(response.close_code == 1000);
    REQUIRE(response.frames.size() == 2);
    REQUIRE(server.captured_payload().find("\"type\":\"response.create\"") != std::string::npos);
}

TEST_CASE(
    "default upstream transport preserves backend websocket continuity for previous_response_id",
    "[proxy][transport][default][ws][continuity]"
) {
    tightrope::proxy::reset_upstream_transport();

    ContinuityAwareWebSocketServer server;

    EnvVarGuard base_url_guard("TIGHTROPE_UPSTREAM_BASE_URL");
    const auto base_url = std::string("http://127.0.0.1:") + std::to_string(server.port());
    REQUIRE(setenv("TIGHTROPE_UPSTREAM_BASE_URL", base_url.c_str(), 1) == 0);

    const tightrope::proxy::openai::HeaderMap ws_headers = {
        {"Connection", "Upgrade"},
        {"Sec-WebSocket-Key", "abc"},
        {"Originator", "codex_cli_rs"},
        {"session_id", "ws-continuity-1"},
    };

    const auto first = tightrope::server::controllers::proxy_responses_websocket(
        "/backend-api/codex/responses",
        R"({"model":"gpt-5.4","input":"turn-one"})",
        ws_headers
    );
    REQUIRE(first.status == 101);
    REQUIRE(first.accepted);
    REQUIRE(first.frames.size() == 2);

    const auto second = tightrope::server::controllers::proxy_responses_websocket(
        "/backend-api/codex/responses",
        R"({"model":"gpt-5.4","input":"turn-two","previous_response_id":"resp_ws_bridge_1"})",
        ws_headers
    );
    REQUIRE(second.status == 101);
    REQUIRE(second.accepted);
    REQUIRE(second.frames.size() == 2);
    REQUIRE(second.frames[1].find("\"resp_ws_bridge_2\"") != std::string::npos);
    REQUIRE(second.frames[1].find("\"previous_response_not_found\"") == std::string::npos);
    REQUIRE(server.request_count() == 2);
    REQUIRE(server.connection_count() == 1);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE(
    "default upstream transport evicts timed-out backend websocket bridge sessions",
    "[proxy][transport][default][ws][continuity][timeout]"
) {
    tightrope::proxy::reset_upstream_transport();

    TimeoutThenRecoverWebSocketServer server;

    EnvVarGuard base_url_guard("TIGHTROPE_UPSTREAM_BASE_URL");
    EnvVarGuard timeout_guard("TIGHTROPE_UPSTREAM_TIMEOUT_MS");
    EnvVarGuard poll_timeout_guard("TIGHTROPE_UPSTREAM_BRIDGE_POLL_TIMEOUT_MS");
    const auto base_url = std::string("http://127.0.0.1:") + std::to_string(server.port());
    REQUIRE(setenv("TIGHTROPE_UPSTREAM_BASE_URL", base_url.c_str(), 1) == 0);
    REQUIRE(setenv("TIGHTROPE_UPSTREAM_TIMEOUT_MS", "300", 1) == 0);
    REQUIRE(setenv("TIGHTROPE_UPSTREAM_BRIDGE_POLL_TIMEOUT_MS", "100", 1) == 0);

    const tightrope::proxy::openai::HeaderMap ws_headers = {
        {"Connection", "Upgrade"},
        {"Sec-WebSocket-Key", "abc"},
        {"Originator", "codex_cli_rs"},
        {"session_id", "ws-timeout-recovery-1"},
    };

    const auto first = tightrope::server::controllers::proxy_responses_websocket(
        "/backend-api/codex/responses",
        R"({"model":"gpt-5.4","input":"turn-timeout"})",
        ws_headers
    );
    REQUIRE(first.status == 101);
    REQUIRE(first.accepted);
    REQUIRE(first.frames.size() == 2);
    REQUIRE(first.frames.back().find("\"resp_ws_timeout_recovered\"") != std::string::npos);

    REQUIRE(server.request_count() == 2);
    REQUIRE(server.connection_count() >= 2);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE(
    "default upstream transport reconnects stale backend websocket bridge for continuation requests",
    "[proxy][transport][default][ws][continuity][stale]"
) {
    tightrope::proxy::reset_upstream_transport();

    CloseAfterFirstTurnWebSocketServer server;

    EnvVarGuard base_url_guard("TIGHTROPE_UPSTREAM_BASE_URL");
    const auto base_url = std::string("http://127.0.0.1:") + std::to_string(server.port());
    REQUIRE(setenv("TIGHTROPE_UPSTREAM_BASE_URL", base_url.c_str(), 1) == 0);

    const tightrope::proxy::openai::HeaderMap ws_headers = {
        {"Connection", "Upgrade"},
        {"Sec-WebSocket-Key", "abc"},
        {"Originator", "codex_cli_rs"},
        {"session_id", "ws-stale-continuation-1"},
    };

    const auto first = tightrope::server::controllers::proxy_responses_websocket(
        "/backend-api/codex/responses",
        R"({"model":"gpt-5.4","input":"turn-one"})",
        ws_headers
    );
    REQUIRE(first.status == 101);
    REQUIRE(first.accepted);
    REQUIRE(first.frames.size() == 2);
    REQUIRE(first.frames.back().find("\"resp_ws_stale_first\"") != std::string::npos);

    const auto second = tightrope::server::controllers::proxy_responses_websocket(
        "/backend-api/codex/responses",
        R"({"model":"gpt-5.4","input":"turn-two","previous_response_id":"resp_ws_stale_first"})",
        ws_headers
    );
    REQUIRE(second.status == 101);
    REQUIRE(second.accepted);
    REQUIRE(second.frames.size() == 2);
    REQUIRE(second.frames.back().find("\"resp_ws_stale_second\"") != std::string::npos);
    REQUIRE(server.request_count() == 2);
    REQUIRE(server.connection_count() >= 2);

    tightrope::proxy::reset_upstream_transport();
}

TEST_CASE(
    "default upstream transport processes two backend codex sessions in parallel without freeze",
    "[proxy][transport][default][ws][continuity][parallel]"
) {
    tightrope::proxy::reset_upstream_transport();

    ParallelSessionWebSocketServer server;

    EnvVarGuard base_url_guard("TIGHTROPE_UPSTREAM_BASE_URL");
    const auto base_url = std::string("http://127.0.0.1:") + std::to_string(server.port());
    REQUIRE(setenv("TIGHTROPE_UPSTREAM_BASE_URL", base_url.c_str(), 1) == 0);

    tightrope::server::controllers::ProxySseResponse first{};
    tightrope::server::controllers::ProxySseResponse second{};
    const auto started_at = std::chrono::steady_clock::now();

    std::thread first_thread([&] {
        first = tightrope::server::controllers::post_proxy_responses_sse(
            "/backend-api/codex/responses",
            R"({"model":"gpt-5.4","input":"session-a"})",
            {{"session_id", "parallel-session-a"}}
        );
    });
    std::thread second_thread([&] {
        second = tightrope::server::controllers::post_proxy_responses_sse(
            "/backend-api/codex/responses",
            R"({"model":"gpt-5.4","input":"session-b"})",
            {{"session_id", "parallel-session-b"}}
        );
    });

    first_thread.join();
    second_thread.join();
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_at).count();

    REQUIRE(first.status == 200);
    REQUIRE(second.status == 200);
    REQUIRE(first.events.size() >= 2);
    REQUIRE(second.events.size() >= 2);
    REQUIRE(server.connection_count() >= 2);
    REQUIRE(server.request_count() == 2);
    REQUIRE(elapsed_ms < 900);

    tightrope::proxy::reset_upstream_transport();
}
