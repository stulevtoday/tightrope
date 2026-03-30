#include <catch2/catch_test_macros.hpp>

#include <string>

#include "oauth/callback_server.h"
#include "server/runtime_test_utils.h"

TEST_CASE("oauth callback server serves callback endpoint", "[auth][oauth][callback_server]") {
    auto& callback_server = tightrope::auth::oauth::CallbackServer::instance();
    (void)callback_server.stop();

    const auto callback_port = tightrope::tests::server::next_runtime_port();
    REQUIRE(callback_server.start({
        .host = "127.0.0.1",
        .port = callback_port,
    }));
    REQUIRE(callback_server.is_running());

    const auto response = tightrope::tests::server::send_raw_http(
        callback_port,
        "GET /auth/callback?code=example&state=invalid HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(response.find("200 OK") != std::string::npos);
    REQUIRE(response.find("OAuth Error") != std::string::npos);

    REQUIRE(callback_server.stop());
    REQUIRE_FALSE(callback_server.is_running());
}
