#include "logging/logger.h"

#include <mutex>
#include <string>

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <quill/sinks/ConsoleSink.h>

namespace tightrope::core::logging {

namespace {

quill::Logger* logger_instance() {
    static std::once_flag once;
    static quill::Logger* logger = nullptr;
    std::call_once(once, [] {
        auto sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("stdout");
        logger = quill::Frontend::create_or_get_logger(
            "tightrope-native",
            std::move(sink),
            quill::PatternFormatterOptions{"%(time) [%(log_level)] %(message)"}
        );
        logger->set_log_level(quill::LogLevel::TraceL3);
        quill::Backend::start();
    });
    return logger;
}

LogObserver& log_observer() {
    // Avoid static-destruction order hazards when late process teardown still emits logs.
    static auto* observer = new LogObserver();
    return *observer;
}

std::mutex& log_observer_mutex() {
    // Keep mutex alive until process exit to avoid lock-on-destroyed-mutex during teardown.
    static auto* mutex = new std::mutex();
    return *mutex;
}

std::string build_message(
    const std::string_view domain,
    const std::string_view component,
    const std::string_view event,
    const std::string_view detail) {
    std::string message;
    message.reserve(domain.size() + component.size() + event.size() + detail.size() + 16);
    message.append("[");
    message.append(domain);
    message.append("] [");
    message.append(component);
    message.append("] ");
    message.append(event);
    if (!detail.empty()) {
        message.push_back(' ');
        message.append(detail);
    }
    return message;
}

} // namespace

void log_event(
    const LogLevel level,
    const std::string_view domain,
    const std::string_view component,
    const std::string_view event,
    const std::string_view detail) {
    const auto message = build_message(domain, component, event, detail);

    {
        std::lock_guard<std::mutex> lock(log_observer_mutex());
        if (log_observer()) {
            log_observer()({
                .level = level,
                .domain = std::string(domain),
                .component = std::string(component),
                .event = std::string(event),
                .detail = std::string(detail),
            });
        }
    }

    auto* logger = logger_instance();
    switch (level) {
    case LogLevel::Trace:
        LOG_TRACE_L3(logger, "{}", message);
        break;
    case LogLevel::Debug:
        LOG_DEBUG(logger, "{}", message);
        break;
    case LogLevel::Info:
        LOG_INFO(logger, "{}", message);
        break;
    case LogLevel::Warning:
        LOG_WARNING(logger, "{}", message);
        break;
    case LogLevel::Error:
        LOG_ERROR(logger, "{}", message);
        break;
    }
}

void set_log_observer_for_tests(LogObserver observer) {
    std::lock_guard<std::mutex> lock(log_observer_mutex());
    log_observer() = std::move(observer);
}

void clear_log_observer_for_tests() {
    std::lock_guard<std::mutex> lock(log_observer_mutex());
    log_observer() = {};
}

} // namespace tightrope::core::logging
