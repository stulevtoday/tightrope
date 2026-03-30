#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

// HTTP server setup

namespace tightrope::server {

struct HealthStatus {
    std::string status = "ok";
    std::uint64_t uptime_ms = 0;
};

struct RuntimeConfig {
    std::string host = "127.0.0.1";
    std::uint16_t port = 2455;
};

class Runtime {
  public:
    Runtime() noexcept;
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    [[nodiscard]] bool start(const RuntimeConfig& config = {}) noexcept;
    bool stop() noexcept;
    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] HealthStatus get_health() const noexcept;

  private:
    class Impl;
    using Clock = std::chrono::steady_clock;

    Clock::time_point started_at_{};
    std::unique_ptr<Impl> impl_;
};

} // namespace tightrope::server
