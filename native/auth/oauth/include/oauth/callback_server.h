#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include "server.h"

namespace tightrope::auth::oauth {

struct CallbackServerConfig {
    std::string host = "localhost";
    std::uint16_t port = 1455;
};

class CallbackServer final {
  public:
    static CallbackServer& instance();

    bool start(const CallbackServerConfig& config) noexcept;
    bool stop() noexcept;
    [[nodiscard]] bool is_running() const noexcept;

  private:
    CallbackServer() = default;
    CallbackServer(const CallbackServer&) = delete;
    CallbackServer& operator=(const CallbackServer&) = delete;

    mutable std::mutex mutex_{};
    server::Runtime runtime_{};
    CallbackServerConfig active_config_{};
    bool running_ = false;
};

} // namespace tightrope::auth::oauth
