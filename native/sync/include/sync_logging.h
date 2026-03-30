#pragma once

#include <string_view>

namespace tightrope::sync {

enum class SyncLogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
};

void log_sync_event(
    SyncLogLevel level,
    std::string_view component,
    std::string_view event,
    std::string_view detail = {}
);

void log_discovery_event(
    SyncLogLevel level,
    std::string_view component,
    std::string_view event,
    std::string_view detail = {}
);

} // namespace tightrope::sync
