#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace tightrope::core::logging {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
};

struct LogRecord {
    LogLevel level = LogLevel::Info;
    std::string domain;
    std::string component;
    std::string event;
    std::string detail;
};

using LogObserver = std::function<void(const LogRecord&)>;

void log_event(
    LogLevel level,
    std::string_view domain,
    std::string_view component,
    std::string_view event,
    std::string_view detail = {}
);

void set_log_observer_for_tests(LogObserver observer);
void clear_log_observer_for_tests();

} // namespace tightrope::core::logging
