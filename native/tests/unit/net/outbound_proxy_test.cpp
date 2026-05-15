#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <optional>
#include <string>

#include "net/outbound_proxy.h"

namespace {

class EnvVarGuard final {
public:
    explicit EnvVarGuard(std::string name) : name_(std::move(name)) {
        if (const char* existing = std::getenv(name_.c_str()); existing != nullptr) {
            original_ = existing;
        }
    }

    ~EnvVarGuard() {
        if (original_.has_value()) {
            setenv(name_.c_str(), original_->c_str(), 1);
            return;
        }
        unsetenv(name_.c_str());
    }

private:
    std::string name_;
    std::optional<std::string> original_;
};

} // namespace

TEST_CASE("outbound proxy parser accepts local socks5h endpoint", "[net][proxy]") {
    std::string error;
    const auto proxy = tightrope::core::net::parse_outbound_proxy_url("socks5h://127.0.0.1:10808", &error);

    REQUIRE(proxy.has_value());
    REQUIRE(error.empty());
    REQUIRE(proxy->scheme == "socks5h");
    REQUIRE(proxy->host == "127.0.0.1");
    REQUIRE(proxy->port == 10808);
    REQUIRE(proxy->remote_dns);
    REQUIRE(proxy->url == "socks5h://127.0.0.1:10808");
}

TEST_CASE("outbound proxy parser defaults bare host port to socks5h", "[net][proxy]") {
    std::string error;
    const auto proxy = tightrope::core::net::parse_outbound_proxy_url("127.0.0.1:10808", &error);

    REQUIRE(proxy.has_value());
    REQUIRE(error.empty());
    REQUIRE(proxy->scheme == "socks5h");
    REQUIRE(proxy->remote_dns);
}

TEST_CASE("outbound proxy parser rejects unsupported proxy schemes", "[net][proxy]") {
    std::string error;
    const auto proxy = tightrope::core::net::parse_outbound_proxy_url("vless://example.com:443", &error);

    REQUIRE_FALSE(proxy.has_value());
    REQUIRE(error.find("unsupported proxy scheme") != std::string::npos);
}

TEST_CASE("outbound proxy environment fails closed on invalid configuration", "[net][proxy]") {
    EnvVarGuard guard("TIGHTROPE_OUTBOUND_PROXY_URL");
    REQUIRE(setenv("TIGHTROPE_OUTBOUND_PROXY_URL", "not-a-host-port", 1) == 0);

    std::string error;
    const auto proxy = tightrope::core::net::outbound_proxy_from_env(&error);

    REQUIRE_FALSE(proxy.has_value());
    REQUIRE_FALSE(error.empty());
}
