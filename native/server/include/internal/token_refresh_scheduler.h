#pragma once

#include <cstddef>
#include <cstdint>
#include <stop_token>

#include <sqlite3.h>

namespace tightrope::server::internal::token_refresh {

struct SchedulerConfig {
    std::int64_t startup_delay_ms = 2000;
    std::int64_t startup_stale_after_ms = 4LL * 60LL * 60LL * 1000LL;
    std::int64_t periodic_check_interval_ms = 15LL * 60LL * 1000LL;
    std::int64_t success_cycle_ms = 24LL * 60LL * 60LL * 1000LL;
    std::int64_t failure_retry_ms = 4LL * 60LL * 60LL * 1000LL;
    std::int64_t stop_poll_interval_ms = 1000;
};

enum class SweepMode {
    startup,
    periodic,
};

struct SweepResult {
    std::size_t scanned = 0;
    std::size_t due = 0;
    std::size_t refreshed = 0;
    std::size_t failed = 0;
};

[[nodiscard]] SchedulerConfig load_scheduler_config_from_env() noexcept;
[[nodiscard]] SweepResult run_sweep_once(sqlite3* db, std::int64_t now_ms, const SchedulerConfig& config, SweepMode mode)
    noexcept;
void run_scheduler_loop(const std::stop_token& stop_token, const SchedulerConfig& config) noexcept;

} // namespace tightrope::server::internal::token_refresh
