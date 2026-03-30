#include <catch2/catch_test_macros.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <array>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>

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
        ws.close(websocket::close_code::normal, close_ec);
    }

    int listen_fd_ = -1;
    std::thread thread_;
    std::uint16_t port_ = 0;
    std::vector<std::string> response_frames_;
    std::string captured_payload_;
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

TEST_CASE("default upstream transport supports websocket fallback via SSE transcript", "[proxy][transport][default]") {
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

    REQUIRE(response.status == 101);
    REQUIRE(response.accepted);
    REQUIRE(response.close_code == 1000);
    REQUIRE_FALSE(response.frames.empty());
    REQUIRE(server.captured_request().find("POST /codex/responses HTTP/1.1") != std::string::npos);
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
