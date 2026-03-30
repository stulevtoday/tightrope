#pragma once

#include <string_view>

namespace tightrope::sync::consensus {

enum class ConsensusLogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
};

void log_consensus_event(
    ConsensusLogLevel level,
    std::string_view component,
    std::string_view event,
    std::string_view detail = {}
);

} // namespace tightrope::sync::consensus
