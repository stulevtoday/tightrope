#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "config_loader.h"

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

    void set(std::string value) {
        setenv(name_.c_str(), value.c_str(), 1);
    }

    void unset() {
        unsetenv(name_.c_str());
    }

  private:
    std::string name_;
    std::optional<std::string> original_;
};

struct ConfigEnvScope final {
    EnvVarGuard host{"TIGHTROPE_HOST"};
    EnvVarGuard port{"TIGHTROPE_PORT"};
    EnvVarGuard db_path{"TIGHTROPE_DB_PATH"};
    EnvVarGuard config_path{"TIGHTROPE_CONFIG_PATH"};
    EnvVarGuard log_level{"TIGHTROPE_LOG_LEVEL"};

    ConfigEnvScope() {
        host.unset();
        port.unset();
        db_path.unset();
        config_path.unset();
        log_level.unset();
    }
};

std::filesystem::path next_temp_toml_path() {
    static std::atomic<std::uint64_t> sequence{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path()
        / ("tightrope-config-test-" + std::to_string(now) + "-" + std::to_string(sequence.fetch_add(1)) + ".toml");
}

class TempTomlFile final {
  public:
    explicit TempTomlFile(const std::string& content) : path_(next_temp_toml_path()) {
        std::ofstream file(path_);
        file << content;
        file.flush();
    }

    ~TempTomlFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("config loader applies explicit db path", "[config]") {
    ConfigEnvScope env_scope;

    auto config = tightrope::config::load_config({
        .db_path = "/tmp/tightrope-test.db",
    });

    REQUIRE(config.db_path == "/tmp/tightrope-test.db");
}

TEST_CASE("config loader keeps sane defaults", "[config]") {
    ConfigEnvScope env_scope;

    auto config = tightrope::config::load_config();

    REQUIRE(config.host == "127.0.0.1");
    REQUIRE(config.port == 2455);
}

TEST_CASE("config loader reads TOML values from override config path", "[config]") {
    ConfigEnvScope env_scope;
    const TempTomlFile config_file{
        "host = \"10.0.0.8\"\n"
        "port = 4900\n"
        "db_path = \"/tmp/tightrope-from-file.db\"\n"
        "log_level = \"debug\"\n"};

    REQUIRE(std::filesystem::exists(config_file.path()));

    auto config = tightrope::config::load_config({
        .config_path = config_file.path().string(),
    });

    REQUIRE(config.host == "10.0.0.8");
    REQUIRE(config.port == 4900);
    REQUIRE(config.db_path == "/tmp/tightrope-from-file.db");
    REQUIRE(config.log_level == "debug");
    REQUIRE(config.config_path == config_file.path().string());
}

TEST_CASE("config loader reads TOML path from env and env overrides file values", "[config]") {
    ConfigEnvScope env_scope;
    const TempTomlFile config_file{
        "host = \"10.0.0.9\"\n"
        "port = 4800\n"
        "db_path = \"/tmp/tightrope-from-toml.db\"\n"
        "log_level = \"warn\"\n"};

    REQUIRE(std::filesystem::exists(config_file.path()));
    env_scope.config_path.set(config_file.path().string());
    env_scope.host.set("127.7.7.7");
    env_scope.port.set("6123");

    auto config = tightrope::config::load_config();

    REQUIRE(config.config_path == config_file.path().string());
    REQUIRE(config.host == "127.7.7.7");
    REQUIRE(config.port == 6123);
    REQUIRE(config.db_path == "/tmp/tightrope-from-toml.db");
    REQUIRE(config.log_level == "warn");
}

TEST_CASE("config loader applies explicit overrides after env and TOML", "[config]") {
    ConfigEnvScope env_scope;
    const TempTomlFile config_file{
        "host = \"10.0.0.10\"\n"
        "port = 4700\n"
        "db_path = \"/tmp/tightrope-file.db\"\n"
        "log_level = \"trace\"\n"};

    REQUIRE(std::filesystem::exists(config_file.path()));
    env_scope.config_path.set(config_file.path().string());
    env_scope.host.set("127.8.8.8");
    env_scope.port.set("6500");
    env_scope.db_path.set("/tmp/tightrope-env.db");
    env_scope.log_level.set("error");

    auto config = tightrope::config::load_config({
        .host = "0.0.0.0",
        .port = 2456,
        .db_path = "/tmp/tightrope-override.db",
        .log_level = "info",
    });

    REQUIRE(config.host == "0.0.0.0");
    REQUIRE(config.port == 2456);
    REQUIRE(config.db_path == "/tmp/tightrope-override.db");
    REQUIRE(config.log_level == "info");
    REQUIRE(config.config_path == config_file.path().string());
}
