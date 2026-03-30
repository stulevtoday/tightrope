#include "consensus/logging.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

#include "logging/logger.h"

namespace tightrope::sync::consensus {

namespace {

std::string lowercase(std::string raw) {
    std::transform(raw.begin(), raw.end(), raw.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return raw;
}

bool sync_debug_mode_enabled() {
    const char* raw = std::getenv("TIGHTROPE_SYNC_DEBUG");
    if (raw == nullptr || raw[0] == '\0') {
        return false;
    }
    const std::string value = lowercase(raw);
    return value == "1" || value == "true" || value == "on" || value == "yes";
}

ConsensusLogLevel parse_level_from_env() {
    const char* raw = std::getenv("TIGHTROPE_CONSENSUS_LOG_LEVEL");
    if (raw == nullptr || raw[0] == '\0') {
        return sync_debug_mode_enabled() ? ConsensusLogLevel::Trace : ConsensusLogLevel::Info;
    }

    const auto level = lowercase(raw);
    if (level == "trace") {
        return ConsensusLogLevel::Trace;
    }
    if (level == "debug") {
        return ConsensusLogLevel::Debug;
    }
    if (level == "info") {
        return ConsensusLogLevel::Info;
    }
    if (level == "warning" || level == "warn") {
        return ConsensusLogLevel::Warning;
    }
    if (level == "error") {
        return ConsensusLogLevel::Error;
    }
    return sync_debug_mode_enabled() ? ConsensusLogLevel::Trace : ConsensusLogLevel::Info;
}

bool logging_enabled() {
    const char* raw = std::getenv("TIGHTROPE_CONSENSUS_LOG");
    if (raw == nullptr || raw[0] == '\0') {
        return true;
    }
    const std::string value = lowercase(raw);
    return value == "1" || value == "true" || value == "on" || value == "yes";
}

int severity_rank(const ConsensusLogLevel level) {
    switch (level) {
    case ConsensusLogLevel::Trace:
        return 0;
    case ConsensusLogLevel::Debug:
        return 1;
    case ConsensusLogLevel::Info:
        return 2;
    case ConsensusLogLevel::Warning:
        return 3;
    case ConsensusLogLevel::Error:
        return 4;
    }
    return 2;
}

core::logging::LogLevel to_core_level(const ConsensusLogLevel level) {
    switch (level) {
    case ConsensusLogLevel::Trace:
        return core::logging::LogLevel::Trace;
    case ConsensusLogLevel::Debug:
        return core::logging::LogLevel::Debug;
    case ConsensusLogLevel::Info:
        return core::logging::LogLevel::Info;
    case ConsensusLogLevel::Warning:
        return core::logging::LogLevel::Warning;
    case ConsensusLogLevel::Error:
        return core::logging::LogLevel::Error;
    }
    return core::logging::LogLevel::Info;
}

} // namespace

void log_consensus_event(
    const ConsensusLogLevel level,
    const std::string_view component,
    const std::string_view event,
    const std::string_view detail
) {
    if (!logging_enabled()) {
        return;
    }

    if (severity_rank(level) < severity_rank(parse_level_from_env())) {
        return;
    }

    core::logging::log_event(to_core_level(level), "consensus", component, event, detail);
}

} // namespace tightrope::sync::consensus
