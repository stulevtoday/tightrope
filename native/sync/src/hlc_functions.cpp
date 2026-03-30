#include "hlc_functions.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "checksum.h"
#include "hlc.h"
#include "sync_logging.h"

namespace tightrope::sync {

namespace {

std::uint64_t now_wall_ms() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count()
    );
}

HlcClock* clock_from_ctx(sqlite3_context* ctx) {
    return static_cast<HlcClock*>(sqlite3_user_data(ctx));
}

void hlc_now_wall_fn(sqlite3_context* ctx, int /*argc*/, sqlite3_value** /*argv*/) {
    auto* clock = clock_from_ctx(ctx);
    if (clock == nullptr) {
        log_sync_event(SyncLogLevel::Warning, "hlc_functions", "hlc_now_wall_missing_clock");
        sqlite3_result_error(ctx, "_hlc_now_wall missing clock", -1);
        return;
    }
    const auto hlc = clock->on_local_event(now_wall_ms());
    sqlite3_result_int64(ctx, static_cast<sqlite3_int64>(hlc.wall));
}

void hlc_now_counter_fn(sqlite3_context* ctx, int /*argc*/, sqlite3_value** /*argv*/) {
    auto* clock = clock_from_ctx(ctx);
    if (clock == nullptr) {
        log_sync_event(SyncLogLevel::Warning, "hlc_functions", "hlc_now_counter_missing_clock");
        sqlite3_result_error(ctx, "_hlc_now_counter missing clock", -1);
        return;
    }
    sqlite3_result_int64(ctx, static_cast<sqlite3_int64>(clock->snapshot().counter));
}

void hlc_site_id_fn(sqlite3_context* ctx, int /*argc*/, sqlite3_value** /*argv*/) {
    auto* clock = clock_from_ctx(ctx);
    if (clock == nullptr) {
        log_sync_event(SyncLogLevel::Warning, "hlc_functions", "hlc_site_id_missing_clock");
        sqlite3_result_error(ctx, "_hlc_site_id missing clock", -1);
        return;
    }
    sqlite3_result_int64(ctx, static_cast<sqlite3_int64>(clock->snapshot().site_id));
}

std::string text_or_empty(sqlite3_value* value) {
    if (sqlite3_value_type(value) == SQLITE_NULL) {
        return {};
    }
    const auto* text = reinterpret_cast<const char*>(sqlite3_value_text(value));
    return text == nullptr ? std::string() : std::string(text);
}

void checksum_fn(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    if (argc != 5) {
        log_sync_event(
            SyncLogLevel::Warning,
            "hlc_functions",
            "checksum_wrong_arity",
            "argc=" + std::to_string(argc));
        sqlite3_result_error(ctx, "_checksum requires 5 arguments", -1);
        return;
    }

    const auto checksum = journal_checksum(
        text_or_empty(argv[0]),
        text_or_empty(argv[1]),
        text_or_empty(argv[2]),
        text_or_empty(argv[3]),
        text_or_empty(argv[4])
    );
    sqlite3_result_text(ctx, checksum.c_str(), static_cast<int>(checksum.size()), SQLITE_TRANSIENT);
}

} // namespace

bool register_hlc_functions(sqlite3* db, HlcClock* clock) {
    if (db == nullptr || clock == nullptr) {
        log_sync_event(SyncLogLevel::Warning, "hlc_functions", "register_rejected_invalid_args");
        return false;
    }
    log_sync_event(SyncLogLevel::Debug, "hlc_functions", "register_begin");

    int rc = sqlite3_create_function(db, "_hlc_now_wall", 0, SQLITE_UTF8, clock, hlc_now_wall_fn, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        log_sync_event(
            SyncLogLevel::Error,
            "hlc_functions",
            "register_hlc_now_wall_failed",
            sqlite3_errmsg(db));
        return false;
    }

    rc = sqlite3_create_function(db, "_hlc_now_counter", 0, SQLITE_UTF8, clock, hlc_now_counter_fn, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        log_sync_event(
            SyncLogLevel::Error,
            "hlc_functions",
            "register_hlc_now_counter_failed",
            sqlite3_errmsg(db));
        return false;
    }

    rc = sqlite3_create_function(db, "_hlc_site_id", 0, SQLITE_UTF8, clock, hlc_site_id_fn, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        log_sync_event(
            SyncLogLevel::Error,
            "hlc_functions",
            "register_hlc_site_id_failed",
            sqlite3_errmsg(db));
        return false;
    }

    rc = sqlite3_create_function(db, "_checksum", 5, SQLITE_UTF8, nullptr, checksum_fn, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        log_sync_event(
            SyncLogLevel::Error,
            "hlc_functions",
            "register_checksum_failed",
            sqlite3_errmsg(db));
        return false;
    }

    log_sync_event(SyncLogLevel::Debug, "hlc_functions", "register_complete");
    return true;
}

} // namespace tightrope::sync
