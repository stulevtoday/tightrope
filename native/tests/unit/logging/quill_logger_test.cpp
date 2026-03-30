#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "logging/logger.h"
#include "consensus/logging.h"
#include "sync_logging.h"

namespace {

class EnvVarGuard final {
public:
    explicit EnvVarGuard(std::string name) : name_(std::move(name)) {
        if (const char* existing = std::getenv(name_.c_str()); existing != nullptr) {
            original_ = std::string(existing);
        }
    }

    ~EnvVarGuard() {
        if (original_.has_value()) {
            setenv(name_.c_str(), original_->c_str(), 1);
            return;
        }
        unsetenv(name_.c_str());
    }

    void set(const std::string& value) {
        setenv(name_.c_str(), value.c_str(), 1);
    }

private:
    std::string name_;
    std::optional<std::string> original_;
};

} // namespace

TEST_CASE("core logger publishes structured records to observer hook", "[logging][core]") {
    std::vector<tightrope::core::logging::LogRecord> observed;
    tightrope::core::logging::set_log_observer_for_tests(
        [&observed](const tightrope::core::logging::LogRecord& record) { observed.push_back(record); });

    tightrope::core::logging::log_event(
        tightrope::core::logging::LogLevel::Info, "runtime", "server", "start_complete", "port=2455");
    tightrope::core::logging::clear_log_observer_for_tests();

    REQUIRE(observed.size() == 1);
    REQUIRE(observed.front().domain == "runtime");
    REQUIRE(observed.front().component == "server");
    REQUIRE(observed.front().event == "start_complete");
    REQUIRE(observed.front().detail == "port=2455");
}

TEST_CASE("consensus logging obeys enable and minimum level env controls", "[logging][consensus]") {
    EnvVarGuard enable_guard("TIGHTROPE_CONSENSUS_LOG");
    EnvVarGuard level_guard("TIGHTROPE_CONSENSUS_LOG_LEVEL");

    std::vector<tightrope::core::logging::LogRecord> observed;
    tightrope::core::logging::set_log_observer_for_tests(
        [&observed](const tightrope::core::logging::LogRecord& record) { observed.push_back(record); });

    enable_guard.set("0");
    level_guard.set("trace");
    tightrope::sync::consensus::log_consensus_event(
        tightrope::sync::consensus::ConsensusLogLevel::Error, "raft_log", "disabled_should_drop");
    REQUIRE(observed.empty());

    enable_guard.set("1");
    level_guard.set("warning");
    tightrope::sync::consensus::log_consensus_event(
        tightrope::sync::consensus::ConsensusLogLevel::Info, "raft_log", "below_threshold_should_drop");
    REQUIRE(observed.empty());

    tightrope::sync::consensus::log_consensus_event(
        tightrope::sync::consensus::ConsensusLogLevel::Error, "raft_log", "error_should_log", "reason=test");
    tightrope::core::logging::clear_log_observer_for_tests();

    REQUIRE(observed.size() == 1);
    REQUIRE(observed.front().domain == "consensus");
    REQUIRE(observed.front().component == "raft_log");
    REQUIRE(observed.front().event == "error_should_log");
    REQUIRE(observed.front().detail == "reason=test");
}

TEST_CASE("sync logging honors level filters and sync debug override", "[logging][sync]") {
    EnvVarGuard enable_guard("TIGHTROPE_SYNC_LOG");
    EnvVarGuard level_guard("TIGHTROPE_SYNC_LOG_LEVEL");
    EnvVarGuard debug_guard("TIGHTROPE_SYNC_DEBUG");

    std::vector<tightrope::core::logging::LogRecord> observed;
    tightrope::core::logging::set_log_observer_for_tests(
        [&observed](const tightrope::core::logging::LogRecord& record) { observed.push_back(record); });

    enable_guard.set("1");
    level_guard.set("warning");
    debug_guard.set("0");

    tightrope::sync::log_sync_event(tightrope::sync::SyncLogLevel::Debug, "sync_engine", "debug_should_drop");
    REQUIRE(observed.empty());

    tightrope::sync::log_sync_event(
        tightrope::sync::SyncLogLevel::Error, "sync_engine", "error_should_log", "reason=test");
    REQUIRE(observed.size() == 1);
    REQUIRE(observed.front().domain == "sync");
    REQUIRE(observed.front().component == "sync_engine");
    REQUIRE(observed.front().event == "error_should_log");

    observed.clear();
    unsetenv("TIGHTROPE_SYNC_LOG_LEVEL");
    debug_guard.set("1");
    tightrope::sync::log_discovery_event(
        tightrope::sync::SyncLogLevel::Trace, "mdns_browser", "trace_enabled_by_debug");
    tightrope::core::logging::clear_log_observer_for_tests();

    REQUIRE(observed.size() == 1);
    REQUIRE(observed.front().domain == "discovery");
    REQUIRE(observed.front().component == "mdns_browser");
    REQUIRE(observed.front().event == "trace_enabled_by_debug");
}
