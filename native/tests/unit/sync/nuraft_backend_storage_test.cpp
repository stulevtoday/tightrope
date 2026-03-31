#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

#include "consensus/internal/nuraft_backend_components.h"

namespace {

std::filesystem::path make_temp_storage_path() {
    static std::atomic<std::uint64_t> sequence{1};
    const auto tick = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto unique = std::to_string(tick) + "-" + std::to_string(sequence.fetch_add(1));
    auto root = std::filesystem::temp_directory_path() / "tightrope-raft-storage-tests" / unique;
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    REQUIRE_FALSE(ec);
    return root / "raft.db";
}

} // namespace

TEST_CASE("sqlite raft storage flush truncates wal file", "[sync][raft][storage][p0]") {
    namespace internal = tightrope::sync::consensus::nuraft_backend::internal;

    const auto db_path = make_temp_storage_path();
    const auto wal_path = std::filesystem::path(db_path.string() + "-wal");
    std::error_code ec;

    internal::SqliteRaftStorage storage(db_path.string());
    REQUIRE(storage.open());

    constexpr char kPayload[] = "checkpoint-me";
    REQUIRE(storage.append_committed(1, kPayload, sizeof(kPayload) - 1));
    REQUIRE(storage.append_committed(2, kPayload, sizeof(kPayload) - 1));

    REQUIRE(std::filesystem::exists(wal_path));
    const auto wal_size_before = std::filesystem::file_size(wal_path, ec);
    REQUIRE_FALSE(ec);
    REQUIRE(wal_size_before > 0);

    REQUIRE(storage.flush());

    if (std::filesystem::exists(wal_path)) {
        const auto wal_size_after = std::filesystem::file_size(wal_path, ec);
        REQUIRE_FALSE(ec);
        REQUIRE(wal_size_after == 0);
    }

    storage.close();
    std::filesystem::remove_all(db_path.parent_path(), ec);
}
