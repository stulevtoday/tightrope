#include "sync_logging.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

#include "logging/logger.h"

namespace tightrope::sync {

namespace {

std::string lowercase(std::string raw) {
    std::transform(raw.begin(), raw.end(), raw.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return raw;
}

bool env_truthy(const char* raw) {
    if (raw == nullptr || raw[0] == '\0') {
        return false;
    }
    const auto value = lowercase(raw);
    return value == "1" || value == "true" || value == "on" || value == "yes";
}

SyncLogLevel parse_level(const std::string_view raw, const SyncLogLevel fallback) {
    const auto value = lowercase(std::string(raw));
    if (value == "trace") {
        return SyncLogLevel::Trace;
    }
    if (value == "debug") {
        return SyncLogLevel::Debug;
    }
    if (value == "info") {
        return SyncLogLevel::Info;
    }
    if (value == "warning" || value == "warn") {
        return SyncLogLevel::Warning;
    }
    if (value == "error") {
        return SyncLogLevel::Error;
    }
    return fallback;
}

bool debug_mode_enabled() {
    return env_truthy(std::getenv("TIGHTROPE_SYNC_DEBUG"));
}

bool logging_enabled() {
    const char* raw = std::getenv("TIGHTROPE_SYNC_LOG");
    if (raw == nullptr || raw[0] == '\0') {
        return true;
    }
    return env_truthy(raw);
}

SyncLogLevel minimum_level() {
    const auto fallback = debug_mode_enabled() ? SyncLogLevel::Trace : SyncLogLevel::Info;
    const char* raw = std::getenv("TIGHTROPE_SYNC_LOG_LEVEL");
    if (raw == nullptr || raw[0] == '\0') {
        return fallback;
    }
    return parse_level(raw, fallback);
}

int severity_rank(const SyncLogLevel level) {
    switch (level) {
    case SyncLogLevel::Trace:
        return 0;
    case SyncLogLevel::Debug:
        return 1;
    case SyncLogLevel::Info:
        return 2;
    case SyncLogLevel::Warning:
        return 3;
    case SyncLogLevel::Error:
        return 4;
    }
    return 2;
}

tightrope::core::logging::LogLevel to_core_level(const SyncLogLevel level) {
    switch (level) {
    case SyncLogLevel::Trace:
        return tightrope::core::logging::LogLevel::Trace;
    case SyncLogLevel::Debug:
        return tightrope::core::logging::LogLevel::Debug;
    case SyncLogLevel::Info:
        return tightrope::core::logging::LogLevel::Info;
    case SyncLogLevel::Warning:
        return tightrope::core::logging::LogLevel::Warning;
    case SyncLogLevel::Error:
        return tightrope::core::logging::LogLevel::Error;
    }
    return tightrope::core::logging::LogLevel::Info;
}

void log_domain_event(
    const SyncLogLevel level,
    const std::string_view domain,
    const std::string_view component,
    const std::string_view event,
    const std::string_view detail
) {
    if (!logging_enabled()) {
        return;
    }
    if (severity_rank(level) < severity_rank(minimum_level())) {
        return;
    }
    tightrope::core::logging::log_event(to_core_level(level), domain, component, event, detail);
}

} // namespace

void log_sync_event(
    const SyncLogLevel level,
    const std::string_view component,
    const std::string_view event,
    const std::string_view detail
) {
    log_domain_event(level, "sync", component, event, detail);
}

void log_discovery_event(
    const SyncLogLevel level,
    const std::string_view component,
    const std::string_view event,
    const std::string_view detail
) {
    log_domain_event(level, "discovery", component, event, detail);
}

} // namespace tightrope::sync
