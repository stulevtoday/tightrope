#include "server.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include <uwebsockets/App.h>

#include "logging/logger.h"
#include "account_traffic.h"
#include "internal/server_routes.h"

namespace tightrope::server {

class Runtime::Impl {
public:
    mutable std::mutex mutex;
    std::condition_variable startup_cv;
    std::thread worker;
    bool startup_done = false;
    bool startup_ok = false;
    bool running = false;
    uWS::Loop* loop = nullptr;
    uWS::App* app = nullptr;
};

Runtime::Runtime() noexcept : started_at_(Clock::now()), impl_(std::make_unique<Impl>()) {}

Runtime::~Runtime() {
    stop();
}

bool Runtime::start(const RuntimeConfig& config) noexcept {
    if (config.port == 0) {
        core::logging::log_event(
            core::logging::LogLevel::Warning, "runtime", "server", "start_rejected", "reason=invalid_port");
        return false;
    }

    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->running) {
            core::logging::log_event(
                core::logging::LogLevel::Debug, "runtime", "server", "start_ignored", "reason=already_running");
            return true;
        }
        impl_->startup_done = false;
        impl_->startup_ok = false;
    }
    proxy::clear_account_traffic_update_callback();

    impl_->worker = std::thread([this, config] {
        auto app = std::make_unique<uWS::App>();
        internal::wire_routes(*app, this);

        {
            std::lock_guard lock(impl_->mutex);
            impl_->app = app.get();
            impl_->loop = app->getLoop();
        }

        app->listen(config.host, static_cast<int>(config.port), [this, host = config.host, port = config.port](us_listen_socket_t* socket) {
            std::lock_guard lock(impl_->mutex);
            impl_->startup_ok = socket != nullptr;
            impl_->startup_done = true;
            impl_->running = socket != nullptr;
            impl_->startup_cv.notify_all();
            if (socket != nullptr) {
                core::logging::log_event(
                    core::logging::LogLevel::Info,
                    "runtime",
                    "server",
                    "listen_started",
                    "host=" + host + " port=" + std::to_string(port)
                );
            } else {
                core::logging::log_event(
                    core::logging::LogLevel::Error,
                    "runtime",
                    "server",
                    "listen_failed",
                    "host=" + host + " port=" + std::to_string(port)
                );
            }
        });

        {
            std::lock_guard lock(impl_->mutex);
            if (!impl_->startup_done) {
                impl_->startup_ok = false;
                impl_->startup_done = true;
                impl_->running = false;
                impl_->startup_cv.notify_all();
            }
        }

        if (impl_->startup_ok) {
            app->run();
        }

        std::lock_guard lock(impl_->mutex);
        impl_->running = false;
        impl_->app = nullptr;
        impl_->loop = nullptr;
    });

    bool startup_signaled = false;
    bool started = false;
    {
        std::unique_lock lock(impl_->mutex);
        startup_signaled = impl_->startup_cv.wait_for(lock, std::chrono::seconds(5), [this] { return impl_->startup_done; });
        started = startup_signaled && impl_->startup_ok;
    }

    if (!started) {
        const auto reason = startup_signaled ? "listen_failed" : "startup_timeout";
        core::logging::log_event(
            core::logging::LogLevel::Error,
            "runtime",
            "server",
            "start_failed",
            std::string("reason=") + reason
        );
        stop();
        return false;
    }

    started_at_ = Clock::now();
    core::logging::log_event(core::logging::LogLevel::Info, "runtime", "server", "start_complete");
    return true;
}

bool Runtime::stop() noexcept {
    uWS::Loop* loop = nullptr;
    uWS::App* app = nullptr;
    bool had_activity = false;
    {
        std::lock_guard lock(impl_->mutex);
        loop = impl_->loop;
        app = impl_->app;
        had_activity = impl_->running || impl_->startup_done || impl_->startup_ok || impl_->worker.joinable() || loop != nullptr ||
                       app != nullptr;
    }

    if (loop != nullptr && app != nullptr) {
        loop->defer([app] { app->close(); });
    }

    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }
    proxy::clear_account_traffic_update_callback();

    std::lock_guard lock(impl_->mutex);
    impl_->running = false;
    impl_->loop = nullptr;
    impl_->app = nullptr;
    impl_->startup_done = false;
    impl_->startup_ok = false;
    if (had_activity) {
        core::logging::log_event(core::logging::LogLevel::Info, "runtime", "server", "stopped");
    }
    return true;
}

bool Runtime::is_running() const noexcept {
    std::lock_guard lock(impl_->mutex);
    return impl_->running;
}

HealthStatus Runtime::get_health() const noexcept {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started_at_).count();
    if (elapsed < 0) {
        elapsed = 0;
    }
    return {
        .status = "ok",
        .uptime_ms = static_cast<std::uint64_t>(elapsed),
    };
}

} // namespace tightrope::server
