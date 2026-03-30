#include "callback_server.h"

#include "logging/logger.h"

namespace tightrope::auth::oauth {

CallbackServer& CallbackServer::instance() {
    static CallbackServer server;
    return server;
}

bool CallbackServer::start(const CallbackServerConfig& config) noexcept {
    if (config.host.empty() || config.port == 0) {
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "oauth_callback_server",
            "start_rejected",
            "reason=invalid_config"
        );
        return false;
    }

    std::lock_guard lock(mutex_);
    if (running_ && active_config_.host == config.host && active_config_.port == config.port) {
        return true;
    }

    if (running_) {
        runtime_.stop();
        running_ = false;
    }

    if (!runtime_.start({
            .host = config.host,
            .port = config.port,
        })) {
        core::logging::log_event(
            core::logging::LogLevel::Error,
            "runtime",
            "oauth_callback_server",
            "start_failed",
            "host=" + config.host + " port=" + std::to_string(config.port)
        );
        return false;
    }

    active_config_ = config;
    running_ = true;
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "oauth_callback_server",
        "started",
        "host=" + config.host + " port=" + std::to_string(config.port)
    );
    return true;
}

bool CallbackServer::stop() noexcept {
    std::lock_guard lock(mutex_);
    if (!running_) {
        return true;
    }
    runtime_.stop();
    running_ = false;
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "oauth_callback_server",
        "stopped",
        "host=" + active_config_.host + " port=" + std::to_string(active_config_.port)
    );
    return true;
}

bool CallbackServer::is_running() const noexcept {
    std::lock_guard lock(mutex_);
    return running_ && runtime_.is_running();
}

} // namespace tightrope::auth::oauth
