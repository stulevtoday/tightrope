# Sync Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the five foundation gaps in the sync subsystem so CDC triggers, persistent journaling, and cross-node replication have the infrastructure they need.

**Architecture:** Five independent-ish components: (1) register custom SQLite functions the CDC triggers call, (2) expose the list of replicated table names from the conflict resolver, (3) create sync schema (tombstones table + HLC columns on app tables), (4) persistent journal backed by SQLite, (5) fix RPC channel endianness. Each component gets its own header+source+test. All new source files go under `native/sync/` in existing include/src layout. All new test files go under `native/tests/`. CMake auto-discovers both via GLOB_RECURSE -- no CMakeLists.txt changes needed.

**Tech Stack:** C++20, SQLite3 (C API), Catch2, libsodium (for checksum), Boost.UUID (for batch IDs), Boost.ASIO (for buffer_copy in RPC)

**Spec:** `docs/superpowers/specs/2026-03-30-sync-foundation-design.md`

---

## File Map

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `native/sync/include/hlc_functions.h` | Declare `register_hlc_functions()` |
| Create | `native/sync/src/hlc_functions.cpp` | Implement SQLite function registration |
| Create | `native/sync/include/sync_schema.h` | Declare `ensure_sync_schema()` |
| Create | `native/sync/src/sync_schema.cpp` | Create tombstones table + add HLC columns |
| Create | `native/sync/include/persistent_journal.h` | Declare `PersistentJournal` class |
| Create | `native/sync/src/persistent_journal.cpp` | Implement persistent journal over SQLite |
| Modify | `native/sync/include/conflict_resolver.h` | Add `replicated_table_names()` declaration |
| Modify | `native/sync/src/conflict_resolver.cpp` | Implement `replicated_table_names()` |
| Modify | `native/sync/src/transport/rpc_channel.cpp` | Fix endianness to explicit little-endian |
| Create | `native/tests/unit/sync/hlc_functions_test.cpp` | Tests for SQLite function registration |
| Create | `native/tests/integration/sync/sync_schema_test.cpp` | Tests for schema creation |
| Create | `native/tests/integration/sync/persistent_journal_test.cpp` | Tests for persistent journal |
| Modify | `native/tests/unit/sync/conflict_resolver_test.cpp` | Add test for `replicated_table_names()` |
| Modify | `native/tests/unit/sync/rpc_channel_test.cpp` | Add explicit LE wire-format test |

---

### Task 1: Expose Replicated Table Names

**Files:**
- Modify: `native/sync/include/conflict_resolver.h`
- Modify: `native/sync/src/conflict_resolver.cpp`
- Modify: `native/tests/unit/sync/conflict_resolver_test.cpp`

- [ ] **Step 1: Write the failing test**

Add to the end of `native/tests/unit/sync/conflict_resolver_test.cpp`:

```cpp
TEST_CASE("conflict resolver exposes replicated table names", "[sync][conflict]") {
    const auto names = tightrope::sync::replicated_table_names();

    REQUIRE(names.size() == 7);

    auto contains = [&](std::string_view name) {
        return std::find(names.begin(), names.end(), name) != names.end();
    };

    REQUIRE(contains("accounts"));
    REQUIRE(contains("dashboard_settings"));
    REQUIRE(contains("api_keys"));
    REQUIRE(contains("api_key_limits"));
    REQUIRE(contains("ip_allowlist"));
    REQUIRE(contains("usage_history"));
    REQUIRE(contains("sticky_sessions"));

    REQUIRE_FALSE(contains("request_logs"));
    REQUIRE_FALSE(contains("_sync_journal"));
    REQUIRE_FALSE(contains("_sync_meta"));
    REQUIRE_FALSE(contains("_sync_last_seen"));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd build-debug && cmake --build . --target tightrope-tests 2>&1 | tail -20`
Expected: Compilation error -- `replicated_table_names` is not a member of `tightrope::sync`.

- [ ] **Step 3: Add declaration to header**

Add to `native/sync/include/conflict_resolver.h` before the closing `}`:

```cpp
std::vector<std::string_view> replicated_table_names();
```

Also add `#include <vector>` to the includes if not already present.

- [ ] **Step 4: Write implementation**

Add to the end of `native/sync/src/conflict_resolver.cpp` (before the closing namespace brace):

```cpp
std::vector<std::string_view> replicated_table_names() {
    std::vector<std::string_view> names;
    for (const auto& rule : kTableRules) {
        if (rule.replicated) {
            names.push_back(rule.table);
        }
    }
    return names;
}
```

- [ ] **Step 5: Build and run test**

Run: `cd build-debug && cmake --build . --target tightrope-tests 2>&1 | tail -5 && ./tightrope-tests "[sync][conflict]" -v 2>&1 | tail -20`
Expected: All conflict resolver tests pass including the new one.

- [ ] **Step 6: Commit**

```
git add native/sync/include/conflict_resolver.h native/sync/src/conflict_resolver.cpp native/tests/unit/sync/conflict_resolver_test.cpp
git commit -m "feat(sync): expose replicated_table_names from conflict resolver"
```

---

### Task 2: Register Custom SQLite Functions

**Files:**
- Create: `native/sync/include/hlc_functions.h`
- Create: `native/sync/src/hlc_functions.cpp`
- Create: `native/tests/unit/sync/hlc_functions_test.cpp`

- [ ] **Step 1: Write the failing test**

Create `native/tests/unit/sync/hlc_functions_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <string>

#include <sqlite3.h>

#include "checksum.h"
#include "hlc.h"
#include "hlc_functions.h"

namespace {

std::string query_text(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(stmt != nullptr);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    std::string result = text != nullptr ? text : "";
    sqlite3_finalize(stmt);
    return result;
}

std::int64_t query_int(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(stmt != nullptr);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    const auto value = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

} // namespace

TEST_CASE("hlc functions site_id returns configured value", "[sync][hlc_functions]") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);

    tightrope::sync::HlcClock clock(42);
    REQUIRE(tightrope::sync::register_hlc_functions(db, &clock));

    REQUIRE(query_int(db, "SELECT _hlc_site_id();") == 42);

    sqlite3_close(db);
}

TEST_CASE("hlc functions wall returns value close to current time", "[sync][hlc_functions]") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);

    tightrope::sync::HlcClock clock(7);
    REQUIRE(tightrope::sync::register_hlc_functions(db, &clock));

    const auto now_ms = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );

    const auto wall = query_int(db, "SELECT _hlc_now_wall();");
    REQUIRE(wall >= now_ms - 1000);
    REQUIRE(wall <= now_ms + 1000);

    sqlite3_close(db);
}

TEST_CASE("hlc functions counter increments on successive calls", "[sync][hlc_functions]") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);

    tightrope::sync::HlcClock clock(7);
    REQUIRE(tightrope::sync::register_hlc_functions(db, &clock));

    const auto counter1 = query_int(db, "SELECT _hlc_now_wall();");
    const auto c1 = query_int(db, "SELECT _hlc_now_counter();");
    const auto counter2 = query_int(db, "SELECT _hlc_now_wall();");
    const auto c2 = query_int(db, "SELECT _hlc_now_counter();");

    (void)counter1;
    (void)counter2;
    REQUIRE(c2 > c1);

    sqlite3_close(db);
}

TEST_CASE("hlc functions checksum matches journal_checksum", "[sync][hlc_functions]") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);

    tightrope::sync::HlcClock clock(7);
    REQUIRE(tightrope::sync::register_hlc_functions(db, &clock));

    const auto expected = tightrope::sync::journal_checksum(
        "accounts", R"({"id":"1"})", "INSERT", "", R"({"email":"a@x.com"})"
    );

    const auto result = query_text(
        db,
        "SELECT _checksum('accounts', '{\"id\":\"1\"}', 'INSERT', '', '{\"email\":\"a@x.com\"}');"
    );

    REQUIRE(result == expected);

    sqlite3_close(db);
}

TEST_CASE("hlc functions fail gracefully without registration", "[sync][hlc_functions]") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);

    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db, "SELECT _hlc_site_id();", -1, &stmt, nullptr);
    if (rc == SQLITE_OK && stmt != nullptr) {
        REQUIRE(sqlite3_step(stmt) != SQLITE_ROW);
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd build-debug && cmake --build . --target tightrope-tests 2>&1 | tail -20`
Expected: Compilation error -- `hlc_functions.h` not found.

- [ ] **Step 3: Create header**

Create `native/sync/include/hlc_functions.h`:

```cpp
#pragma once
// Register HLC and checksum functions on a SQLite connection for CDC triggers.

#include <sqlite3.h>

namespace tightrope::sync {

class HlcClock;

// Registers _hlc_now_wall(), _hlc_now_counter(), _hlc_site_id(), _checksum()
// on the given connection. clock must outlive all subsequent usage of these functions.
bool register_hlc_functions(sqlite3* db, HlcClock* clock);

} // namespace tightrope::sync
```

- [ ] **Step 4: Create implementation**

Create `native/sync/src/hlc_functions.cpp`:

```cpp
#include "hlc_functions.h"

#include <chrono>
#include <cstdint>
#include <string>

#include "checksum.h"
#include "hlc.h"

namespace tightrope::sync {

namespace {

std::uint64_t now_wall_ms() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

void hlc_now_wall_fn(sqlite3_context* ctx, int /*argc*/, sqlite3_value** /*argv*/) {
    auto* clock = static_cast<HlcClock*>(sqlite3_user_data(ctx));
    const auto hlc = clock->on_local_event(now_wall_ms());
    sqlite3_result_int64(ctx, static_cast<sqlite3_int64>(hlc.wall));
}

void hlc_now_counter_fn(sqlite3_context* ctx, int /*argc*/, sqlite3_value** /*argv*/) {
    auto* clock = static_cast<HlcClock*>(sqlite3_user_data(ctx));
    sqlite3_result_int64(ctx, static_cast<sqlite3_int64>(clock->snapshot().counter));
}

void hlc_site_id_fn(sqlite3_context* ctx, int /*argc*/, sqlite3_value** /*argv*/) {
    auto* clock = static_cast<HlcClock*>(sqlite3_user_data(ctx));
    sqlite3_result_int64(ctx, static_cast<sqlite3_int64>(clock->snapshot().site_id));
}

const char* text_or_empty(sqlite3_value* val) {
    const auto* text = reinterpret_cast<const char*>(sqlite3_value_text(val));
    return text != nullptr ? text : "";
}

void checksum_fn(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    if (argc != 5) {
        sqlite3_result_error(ctx, "_checksum requires 5 arguments", -1);
        return;
    }
    const auto result = journal_checksum(
        text_or_empty(argv[0]),
        text_or_empty(argv[1]),
        text_or_empty(argv[2]),
        text_or_empty(argv[3]),
        text_or_empty(argv[4])
    );
    sqlite3_result_text(ctx, result.c_str(), static_cast<int>(result.size()), SQLITE_TRANSIENT);
}

} // namespace

bool register_hlc_functions(sqlite3* db, HlcClock* clock) {
    if (db == nullptr || clock == nullptr) {
        return false;
    }

    int rc = sqlite3_create_function(db, "_hlc_now_wall", 0, SQLITE_UTF8, clock, hlc_now_wall_fn, nullptr, nullptr);
    if (rc != SQLITE_OK) return false;

    rc = sqlite3_create_function(db, "_hlc_now_counter", 0, SQLITE_UTF8, clock, hlc_now_counter_fn, nullptr, nullptr);
    if (rc != SQLITE_OK) return false;

    rc = sqlite3_create_function(db, "_hlc_site_id", 0, SQLITE_UTF8, clock, hlc_site_id_fn, nullptr, nullptr);
    if (rc != SQLITE_OK) return false;

    rc = sqlite3_create_function(db, "_checksum", 5, SQLITE_UTF8, nullptr, checksum_fn, nullptr, nullptr);
    if (rc != SQLITE_OK) return false;

    return true;
}

} // namespace tightrope::sync
```

- [ ] **Step 5: Build and run tests**

Run: `cd build-debug && cmake --build . --target tightrope-tests 2>&1 | tail -5 && ./tightrope-tests "[sync][hlc_functions]" -v 2>&1 | tail -30`
Expected: All 5 hlc_functions tests pass.

- [ ] **Step 6: Commit**

```
git add native/sync/include/hlc_functions.h native/sync/src/hlc_functions.cpp native/tests/unit/sync/hlc_functions_test.cpp
git commit -m "feat(sync): register HLC and checksum SQLite functions for CDC triggers"
```

---

### Task 3: Sync Schema (Tombstones + HLC Columns)

**Files:**
- Create: `native/sync/include/sync_schema.h`
- Create: `native/sync/src/sync_schema.cpp`
- Create: `native/tests/integration/sync/sync_schema_test.cpp`

- [ ] **Step 1: Write the failing test**

Create `native/tests/integration/sync/sync_schema_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>
#include <unistd.h>

#include <sqlite3.h>

#include "conflict_resolver.h"
#include "sync_schema.h"

namespace {

std::string make_temp_db_path() {
    char path[] = "/tmp/tightrope-sync-schema-XXXXXX";
    const int fd = mkstemp(path);
    REQUIRE(fd != -1);
    close(fd);
    std::remove(path);
    return std::string(path);
}

void exec_sql(sqlite3* db, const char* sql) {
    char* err = nullptr;
    const auto rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (err != nullptr) {
        INFO(std::string(err));
        sqlite3_free(err);
    }
    REQUIRE(rc == SQLITE_OK);
}

bool table_exists(sqlite3* db, const char* table) {
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1;", -1, &stmt, nullptr) == SQLITE_OK);
    sqlite3_bind_text(stmt, 1, table, -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

bool column_exists(sqlite3* db, const char* table, const char* column) {
    const std::string sql = std::string("PRAGMA table_info(") + table + ");";
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (name != nullptr && std::string(name) == column) {
            sqlite3_finalize(stmt);
            return true;
        }
    }
    sqlite3_finalize(stmt);
    return false;
}

void create_minimal_app_tables(sqlite3* db) {
    exec_sql(db, "CREATE TABLE accounts (id INTEGER PRIMARY KEY, email TEXT);");
    exec_sql(db, "CREATE TABLE dashboard_settings (id INTEGER PRIMARY KEY);");
    exec_sql(db, "CREATE TABLE api_keys (id INTEGER PRIMARY KEY, key_id TEXT);");
    exec_sql(db, "CREATE TABLE api_key_limits (id INTEGER PRIMARY KEY, api_key_id INTEGER);");
    exec_sql(db, "CREATE TABLE ip_allowlist (id INTEGER PRIMARY KEY, ip TEXT);");
    exec_sql(db, "CREATE TABLE usage_history (id INTEGER PRIMARY KEY, count INTEGER);");
    exec_sql(db, "CREATE TABLE sticky_sessions (session_key TEXT PRIMARY KEY, account_id TEXT);");
    exec_sql(db, "CREATE TABLE request_logs (id INTEGER PRIMARY KEY, path TEXT);");
}

} // namespace

TEST_CASE("sync schema creates journal and tombstones tables", "[sync][schema][integration]") {
    const auto path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);

    create_minimal_app_tables(db);
    REQUIRE(tightrope::sync::ensure_sync_schema(db));

    REQUIRE(table_exists(db, "_sync_journal"));
    REQUIRE(table_exists(db, "_sync_tombstones"));

    sqlite3_close(db);
    std::remove(path.c_str());
}

TEST_CASE("sync schema adds HLC columns to replicated tables", "[sync][schema][integration]") {
    const auto path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);

    create_minimal_app_tables(db);
    REQUIRE(tightrope::sync::ensure_sync_schema(db));

    const auto replicated = tightrope::sync::replicated_table_names();
    for (const auto& table : replicated) {
        const std::string t(table);
        INFO("Checking table: " << t);
        REQUIRE(column_exists(db, t.c_str(), "_hlc_wall"));
        REQUIRE(column_exists(db, t.c_str(), "_hlc_counter"));
        REQUIRE(column_exists(db, t.c_str(), "_hlc_site"));
    }

    sqlite3_close(db);
    std::remove(path.c_str());
}

TEST_CASE("sync schema does NOT add HLC columns to non-replicated tables", "[sync][schema][integration]") {
    const auto path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);

    create_minimal_app_tables(db);
    REQUIRE(tightrope::sync::ensure_sync_schema(db));

    REQUIRE_FALSE(column_exists(db, "request_logs", "_hlc_wall"));
    REQUIRE_FALSE(column_exists(db, "request_logs", "_hlc_counter"));
    REQUIRE_FALSE(column_exists(db, "request_logs", "_hlc_site"));

    sqlite3_close(db);
    std::remove(path.c_str());
}

TEST_CASE("sync schema is idempotent", "[sync][schema][integration]") {
    const auto path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);

    create_minimal_app_tables(db);
    REQUIRE(tightrope::sync::ensure_sync_schema(db));
    REQUIRE(tightrope::sync::ensure_sync_schema(db));

    REQUIRE(table_exists(db, "_sync_journal"));
    REQUIRE(table_exists(db, "_sync_tombstones"));

    sqlite3_close(db);
    std::remove(path.c_str());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd build-debug && cmake --build . --target tightrope-tests 2>&1 | tail -20`
Expected: Compilation error -- `sync_schema.h` not found.

- [ ] **Step 3: Create header**

Create `native/sync/include/sync_schema.h`:

```cpp
#pragma once
// Create sync infrastructure tables and add HLC columns to replicated tables.

#include <sqlite3.h>

namespace tightrope::sync {

// Creates _sync_journal, _sync_tombstones, and adds _hlc_wall/_hlc_counter/_hlc_site
// columns to all replicated application tables. Idempotent.
bool ensure_sync_schema(sqlite3* db);

} // namespace tightrope::sync
```

- [ ] **Step 4: Create implementation**

Create `native/sync/src/sync_schema.cpp`:

```cpp
#include "sync_schema.h"

#include <string>

#include "conflict_resolver.h"

namespace tightrope::sync {

namespace {

bool exec_sql(sqlite3* db, const char* sql) {
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (err != nullptr) {
        sqlite3_free(err);
    }
    return rc == SQLITE_OK;
}

bool has_column(sqlite3* db, const std::string& table, const std::string& column) {
    const std::string sql = "PRAGMA table_info(" + table + ");";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        return false;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (name != nullptr && column == name) {
            sqlite3_finalize(stmt);
            return true;
        }
    }
    sqlite3_finalize(stmt);
    return false;
}

bool ensure_column(sqlite3* db, const std::string& table, const std::string& column, const char* alter_sql) {
    if (has_column(db, table, column)) {
        return true;
    }
    return exec_sql(db, alter_sql);
}

bool table_exists(sqlite3* db, const std::string& table) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1;", -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, table.c_str(), -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

} // namespace

bool ensure_sync_schema(sqlite3* db) {
    if (db == nullptr) {
        return false;
    }

    if (!exec_sql(db, R"sql(
        CREATE TABLE IF NOT EXISTS _sync_journal (
          seq         INTEGER PRIMARY KEY,
          hlc_wall    INTEGER NOT NULL,
          hlc_counter INTEGER NOT NULL,
          site_id     INTEGER NOT NULL,
          table_name  TEXT    NOT NULL,
          row_pk      TEXT    NOT NULL,
          op          TEXT    NOT NULL,
          old_values  TEXT,
          new_values  TEXT,
          checksum    TEXT    NOT NULL,
          applied     INTEGER DEFAULT 1,
          batch_id    TEXT
        );
    )sql")) {
        return false;
    }

    if (!exec_sql(db, R"sql(
        CREATE TABLE IF NOT EXISTS _sync_tombstones (
          table_name TEXT    NOT NULL,
          row_pk     TEXT    NOT NULL,
          deleted_at INTEGER NOT NULL,
          site_id    INTEGER NOT NULL,
          PRIMARY KEY (table_name, row_pk)
        );
    )sql")) {
        return false;
    }

    for (const auto& name : replicated_table_names()) {
        const std::string table(name);
        if (!table_exists(db, table)) {
            continue;
        }

        const auto wall_sql = "ALTER TABLE " + table + " ADD COLUMN _hlc_wall INTEGER DEFAULT 0;";
        const auto counter_sql = "ALTER TABLE " + table + " ADD COLUMN _hlc_counter INTEGER DEFAULT 0;";
        const auto site_sql = "ALTER TABLE " + table + " ADD COLUMN _hlc_site INTEGER DEFAULT 0;";

        if (!ensure_column(db, table, "_hlc_wall", wall_sql.c_str())) return false;
        if (!ensure_column(db, table, "_hlc_counter", counter_sql.c_str())) return false;
        if (!ensure_column(db, table, "_hlc_site", site_sql.c_str())) return false;
    }

    return true;
}

} // namespace tightrope::sync
```

- [ ] **Step 5: Build and run tests**

Run: `cd build-debug && cmake --build . --target tightrope-tests 2>&1 | tail -5 && ./tightrope-tests "[sync][schema][integration]" -v 2>&1 | tail -30`
Expected: All 4 sync_schema tests pass.

- [ ] **Step 6: Commit**

```
git add native/sync/include/sync_schema.h native/sync/src/sync_schema.cpp native/tests/integration/sync/sync_schema_test.cpp
git commit -m "feat(sync): add sync schema with tombstones table and HLC columns"
```

---

### Task 4: Persistent Journal

**Files:**
- Create: `native/sync/include/persistent_journal.h`
- Create: `native/sync/src/persistent_journal.cpp`
- Create: `native/tests/integration/sync/persistent_journal_test.cpp`

- [ ] **Step 1: Write the failing test**

Create `native/tests/integration/sync/persistent_journal_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>
#include <unistd.h>

#include <sqlite3.h>

#include "persistent_journal.h"
#include "sync_schema.h"

namespace {

std::string make_temp_db_path() {
    char path[] = "/tmp/tightrope-pjournal-XXXXXX";
    const int fd = mkstemp(path);
    REQUIRE(fd != -1);
    close(fd);
    std::remove(path);
    return std::string(path);
}

sqlite3* open_db(const std::string& path) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    REQUIRE(tightrope::sync::ensure_sync_schema(db));
    return db;
}

tightrope::sync::PendingJournalEntry make_pending(
    std::uint64_t wall,
    std::uint32_t counter,
    std::uint32_t site_id,
    std::string table = "accounts",
    std::string pk = R"({"id":"1"})",
    std::string op = "INSERT",
    std::string old_vals = "",
    std::string new_vals = R"({"email":"a@x.com"})",
    std::string batch_id = ""
) {
    tightrope::sync::PendingJournalEntry entry;
    entry.hlc = {.wall = wall, .counter = counter, .site_id = site_id};
    entry.table_name = std::move(table);
    entry.row_pk = std::move(pk);
    entry.op = std::move(op);
    entry.old_values = std::move(old_vals);
    entry.new_values = std::move(new_vals);
    entry.applied = 1;
    entry.batch_id = std::move(batch_id);
    return entry;
}

} // namespace

TEST_CASE("persistent journal append assigns monotonic seq and computes checksum", "[sync][pjournal][integration]") {
    const auto path = make_temp_db_path();
    auto* db = open_db(path);

    tightrope::sync::PersistentJournal journal(db);

    const auto e1 = journal.append(make_pending(100, 1, 7));
    REQUIRE(e1.has_value());
    REQUIRE(e1->seq > 0);
    REQUIRE_FALSE(e1->checksum.empty());
    REQUIRE(e1->checksum.size() == 64);

    const auto e2 = journal.append(make_pending(110, 2, 7));
    REQUIRE(e2.has_value());
    REQUIRE(e2->seq > e1->seq);

    sqlite3_close(db);
    std::remove(path.c_str());
}

TEST_CASE("persistent journal entries survive close and reopen", "[sync][pjournal][integration]") {
    const auto path = make_temp_db_path();

    {
        auto* db = open_db(path);
        tightrope::sync::PersistentJournal journal(db);
        REQUIRE(journal.append(make_pending(100, 1, 7)).has_value());
        REQUIRE(journal.append(make_pending(110, 2, 7)).has_value());
        sqlite3_close(db);
    }

    {
        auto* db = open_db(path);
        tightrope::sync::PersistentJournal journal(db);
        REQUIRE(journal.size() == 2);
        const auto entries = journal.entries_after(0);
        REQUIRE(entries.size() == 2);
        REQUIRE(entries[0].hlc.wall == 100);
        REQUIRE(entries[1].hlc.wall == 110);
        sqlite3_close(db);
    }

    std::remove(path.c_str());
}

TEST_CASE("persistent journal entries_after returns subset by seq", "[sync][pjournal][integration]") {
    const auto path = make_temp_db_path();
    auto* db = open_db(path);

    tightrope::sync::PersistentJournal journal(db);
    const auto e1 = journal.append(make_pending(100, 1, 7));
    const auto e2 = journal.append(make_pending(110, 2, 7));
    const auto e3 = journal.append(make_pending(120, 3, 7));
    REQUIRE(e1.has_value());
    REQUIRE(e2.has_value());
    REQUIRE(e3.has_value());

    const auto after_first = journal.entries_after(e1->seq);
    REQUIRE(after_first.size() == 2);
    REQUIRE(after_first[0].seq == e2->seq);
    REQUIRE(after_first[1].seq == e3->seq);

    const auto after_all = journal.entries_after(e3->seq);
    REQUIRE(after_all.empty());

    sqlite3_close(db);
    std::remove(path.c_str());
}

TEST_CASE("persistent journal rollback_batch removes correct entries", "[sync][pjournal][integration]") {
    const auto path = make_temp_db_path();
    auto* db = open_db(path);

    tightrope::sync::PersistentJournal journal(db);
    journal.append(make_pending(100, 1, 7, "accounts", R"({"id":"1"})", "INSERT", "", R"({"e":"a"})", "batch-a"));
    journal.append(make_pending(110, 2, 7, "accounts", R"({"id":"2"})", "INSERT", "", R"({"e":"b"})", "batch-b"));
    journal.append(make_pending(120, 3, 7, "accounts", R"({"id":"3"})", "INSERT", "", R"({"e":"c"})", "batch-a"));

    const auto removed = journal.rollback_batch("batch-a");
    REQUIRE(removed.size() == 2);
    REQUIRE(removed[0].seq > removed[1].seq); // reverse order

    REQUIRE(journal.size() == 1);
    const auto remaining = journal.entries_after(0);
    REQUIRE(remaining.size() == 1);
    REQUIRE(remaining[0].hlc.wall == 110);

    sqlite3_close(db);
    std::remove(path.c_str());
}

TEST_CASE("persistent journal mark_applied updates flag", "[sync][pjournal][integration]") {
    const auto path = make_temp_db_path();
    auto* db = open_db(path);

    tightrope::sync::PersistentJournal journal(db);
    const auto entry = journal.append(make_pending(100, 1, 7));
    REQUIRE(entry.has_value());
    REQUIRE(entry->applied == 1);

    REQUIRE(journal.mark_applied(entry->seq, 2));

    const auto entries = journal.entries_after(0);
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].applied == 2);

    REQUIRE_FALSE(journal.mark_applied(999999, 2)); // nonexistent seq

    sqlite3_close(db);
    std::remove(path.c_str());
}

TEST_CASE("persistent journal compact removes old acknowledged entries", "[sync][pjournal][integration]") {
    const auto path = make_temp_db_path();
    auto* db = open_db(path);

    tightrope::sync::PersistentJournal journal(db);
    const auto e1 = journal.append(make_pending(100, 1, 7));
    const auto e2 = journal.append(make_pending(200, 2, 7));
    const auto e3 = journal.append(make_pending(300, 3, 7));
    REQUIRE(e1.has_value());
    REQUIRE(e2.has_value());
    REQUIRE(e3.has_value());

    // compact entries with wall < 250 AND seq <= e2->seq
    const auto removed = journal.compact(250, e2->seq);
    REQUIRE(removed == 2);
    REQUIRE(journal.size() == 1);

    const auto remaining = journal.entries_after(0);
    REQUIRE(remaining.size() == 1);
    REQUIRE(remaining[0].seq == e3->seq);

    sqlite3_close(db);
    std::remove(path.c_str());
}

TEST_CASE("persistent journal append auto-generates batch_id when empty", "[sync][pjournal][integration]") {
    const auto path = make_temp_db_path();
    auto* db = open_db(path);

    tightrope::sync::PersistentJournal journal(db);
    const auto entry = journal.append(make_pending(100, 1, 7));
    REQUIRE(entry.has_value());
    REQUIRE_FALSE(entry->batch_id.empty());
    REQUIRE(entry->batch_id.size() == 36); // UUID format

    sqlite3_close(db);
    std::remove(path.c_str());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd build-debug && cmake --build . --target tightrope-tests 2>&1 | tail -20`
Expected: Compilation error -- `persistent_journal.h` not found.

- [ ] **Step 3: Create header**

Create `native/sync/include/persistent_journal.h`:

```cpp
#pragma once
// Journal backed by the _sync_journal SQLite table.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <sqlite3.h>

#include "journal.h"

namespace tightrope::sync {

class PersistentJournal {
public:
    explicit PersistentJournal(sqlite3* db);

    std::optional<JournalEntry> append(const PendingJournalEntry& entry);
    std::vector<JournalEntry> entries_after(std::uint64_t after_seq, std::size_t limit = 1000) const;
    std::vector<JournalEntry> rollback_batch(std::string_view batch_id);
    bool mark_applied(std::uint64_t seq, int applied_value);
    std::size_t compact(std::uint64_t cutoff_wall, std::uint64_t max_ack_seq);
    std::size_t size() const;

private:
    sqlite3* db_;
};

} // namespace tightrope::sync
```

- [ ] **Step 4: Create implementation**

Create `native/sync/src/persistent_journal.cpp`:

```cpp
#include "persistent_journal.h"

#include <algorithm>
#include <string>

#include "checksum.h"
#include "journal_batch_id.h"

namespace tightrope::sync {

namespace {

std::string column_text(sqlite3_stmt* stmt, int col) {
    const auto* text = sqlite3_column_text(stmt, col);
    return text != nullptr ? std::string(reinterpret_cast<const char*>(text)) : std::string();
}

JournalEntry read_entry(sqlite3_stmt* stmt) {
    JournalEntry entry;
    entry.seq = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 0));
    entry.hlc.wall = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 1));
    entry.hlc.counter = static_cast<std::uint32_t>(sqlite3_column_int(stmt, 2));
    entry.hlc.site_id = static_cast<std::uint32_t>(sqlite3_column_int(stmt, 3));
    entry.table_name = column_text(stmt, 4);
    entry.row_pk = column_text(stmt, 5);
    entry.op = column_text(stmt, 6);
    entry.old_values = column_text(stmt, 7);
    entry.new_values = column_text(stmt, 8);
    entry.checksum = column_text(stmt, 9);
    entry.applied = sqlite3_column_int(stmt, 10);
    entry.batch_id = column_text(stmt, 11);
    return entry;
}

} // namespace

PersistentJournal::PersistentJournal(sqlite3* db) : db_(db) {}

std::optional<JournalEntry> PersistentJournal::append(const PendingJournalEntry& entry) {
    if (db_ == nullptr) return std::nullopt;

    const auto batch_id = entry.batch_id.empty() ? generate_batch_id() : entry.batch_id;
    const auto checksum = journal_checksum(
        entry.table_name, entry.row_pk, entry.op, entry.old_values, entry.new_values
    );

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"sql(
        INSERT INTO _sync_journal (hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id)
        VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);
    )sql";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) sqlite3_finalize(stmt);
        return std::nullopt;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(entry.hlc.wall));
    sqlite3_bind_int(stmt, 2, static_cast<int>(entry.hlc.counter));
    sqlite3_bind_int(stmt, 3, static_cast<int>(entry.hlc.site_id));
    sqlite3_bind_text(stmt, 4, entry.table_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, entry.row_pk.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, entry.op.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, entry.old_values.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, entry.new_values.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, checksum.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 10, entry.applied);
    sqlite3_bind_text(stmt, 11, batch_id.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }
    sqlite3_finalize(stmt);

    const auto seq = static_cast<std::uint64_t>(sqlite3_last_insert_rowid(db_));

    JournalEntry result;
    result.seq = seq;
    result.hlc = entry.hlc;
    result.table_name = entry.table_name;
    result.row_pk = entry.row_pk;
    result.op = entry.op;
    result.old_values = entry.old_values;
    result.new_values = entry.new_values;
    result.checksum = checksum;
    result.applied = entry.applied;
    result.batch_id = batch_id;
    return result;
}

std::vector<JournalEntry> PersistentJournal::entries_after(std::uint64_t after_seq, std::size_t limit) const {
    std::vector<JournalEntry> results;
    if (db_ == nullptr) return results;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"sql(
        SELECT seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id
        FROM _sync_journal
        WHERE seq > ?1
        ORDER BY seq ASC
        LIMIT ?2;
    )sql";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) sqlite3_finalize(stmt);
        return results;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(after_seq));
    sqlite3_bind_int(stmt, 2, static_cast<int>(limit));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(read_entry(stmt));
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<JournalEntry> PersistentJournal::rollback_batch(std::string_view batch_id) {
    std::vector<JournalEntry> removed;
    if (db_ == nullptr) return removed;

    // First, read the entries we're about to delete.
    sqlite3_stmt* select_stmt = nullptr;
    const char* select_sql = R"sql(
        SELECT seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id
        FROM _sync_journal
        WHERE batch_id = ?1
        ORDER BY seq DESC;
    )sql";

    if (sqlite3_prepare_v2(db_, select_sql, -1, &select_stmt, nullptr) != SQLITE_OK || select_stmt == nullptr) {
        if (select_stmt != nullptr) sqlite3_finalize(select_stmt);
        return removed;
    }

    const std::string batch_str(batch_id);
    sqlite3_bind_text(select_stmt, 1, batch_str.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(select_stmt) == SQLITE_ROW) {
        removed.push_back(read_entry(select_stmt));
    }
    sqlite3_finalize(select_stmt);

    if (removed.empty()) return removed;

    // Then delete them.
    sqlite3_stmt* delete_stmt = nullptr;
    const char* delete_sql = "DELETE FROM _sync_journal WHERE batch_id = ?1;";

    if (sqlite3_prepare_v2(db_, delete_sql, -1, &delete_stmt, nullptr) != SQLITE_OK || delete_stmt == nullptr) {
        if (delete_stmt != nullptr) sqlite3_finalize(delete_stmt);
        return {};
    }

    sqlite3_bind_text(delete_stmt, 1, batch_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(delete_stmt);
    sqlite3_finalize(delete_stmt);

    return removed;
}

bool PersistentJournal::mark_applied(std::uint64_t seq, int applied_value) {
    if (db_ == nullptr) return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE _sync_journal SET applied = ?1 WHERE seq = ?2;";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_bind_int(stmt, 1, applied_value);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(seq));

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);
    return sqlite3_changes(db_) > 0;
}

std::size_t PersistentJournal::compact(std::uint64_t cutoff_wall, std::uint64_t max_ack_seq) {
    if (db_ == nullptr) return 0;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM _sync_journal WHERE hlc_wall < ?1 AND seq <= ?2;";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(cutoff_wall));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(max_ack_seq));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return static_cast<std::size_t>(sqlite3_changes(db_));
}

std::size_t PersistentJournal::size() const {
    if (db_ == nullptr) return 0;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM _sync_journal;", -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) sqlite3_finalize(stmt);
        return 0;
    }

    std::size_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<std::size_t>(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return count;
}

} // namespace tightrope::sync
```

- [ ] **Step 5: Build and run tests**

Run: `cd build-debug && cmake --build . --target tightrope-tests 2>&1 | tail -5 && ./tightrope-tests "[sync][pjournal][integration]" -v 2>&1 | tail -40`
Expected: All 7 persistent_journal tests pass.

- [ ] **Step 6: Commit**

```
git add native/sync/include/persistent_journal.h native/sync/src/persistent_journal.cpp native/tests/integration/sync/persistent_journal_test.cpp
git commit -m "feat(sync): add PersistentJournal backed by _sync_journal SQLite table"
```

---

### Task 5: Fix RPC Channel Endianness

**Files:**
- Modify: `native/sync/src/transport/rpc_channel.cpp`
- Modify: `native/tests/unit/sync/rpc_channel_test.cpp`

- [ ] **Step 1: Write the failing test**

Add to the end of `native/tests/unit/sync/rpc_channel_test.cpp`:

```cpp
TEST_CASE("rpc channel encodes in little-endian byte order", "[sync][transport][rpc]") {
    const tightrope::sync::transport::RpcFrame frame = {
        .channel = 0x0102,
        .payload = std::vector<std::uint8_t>{'A'},
    };

    const auto bytes = tightrope::sync::transport::RpcChannel::encode(frame);

    // Header: 2 bytes channel (LE) + 4 bytes payload_size (LE) + 1 byte payload
    REQUIRE(bytes.size() == 7);

    // Channel 0x0102 in LE: low byte 0x02, high byte 0x01
    REQUIRE(bytes[0] == 0x02);
    REQUIRE(bytes[1] == 0x01);

    // Payload size 1 in LE: 0x01 0x00 0x00 0x00
    REQUIRE(bytes[2] == 0x01);
    REQUIRE(bytes[3] == 0x00);
    REQUIRE(bytes[4] == 0x00);
    REQUIRE(bytes[5] == 0x00);

    // Payload
    REQUIRE(bytes[6] == 'A');
}

TEST_CASE("rpc channel encodes multi-byte payload size in little-endian", "[sync][transport][rpc]") {
    // Create a payload of 0x01020304 bytes? No -- too large.
    // Instead verify a known smaller size: 258 bytes = 0x00000102.
    std::vector<std::uint8_t> payload(258, 0x42);
    const tightrope::sync::transport::RpcFrame frame = {
        .channel = 1,
        .payload = payload,
    };

    const auto bytes = tightrope::sync::transport::RpcChannel::encode(frame);
    // payload_size = 258 = 0x00000102, LE: 0x02 0x01 0x00 0x00
    REQUIRE(bytes[2] == 0x02);
    REQUIRE(bytes[3] == 0x01);
    REQUIRE(bytes[4] == 0x00);
    REQUIRE(bytes[5] == 0x00);
}
```

- [ ] **Step 2: Run test to verify it fails (on little-endian hosts it may pass since memcpy happens to match -- that's fine, the fix is still correct)**

Run: `cd build-debug && cmake --build . --target tightrope-tests 2>&1 | tail -5 && ./tightrope-tests "[sync][transport][rpc]" -v 2>&1 | tail -20`
Expected: Tests may pass on x86/ARM LE hosts. The fix ensures correctness on any architecture.

- [ ] **Step 3: Replace memcpy encoding with explicit little-endian**

Replace the anonymous namespace helper functions in `native/sync/src/transport/rpc_channel.cpp`. Change the four functions to:

```cpp
void write_u16(std::vector<std::uint8_t>& out, const std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void write_u32(std::vector<std::uint8_t>& out, const std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

std::uint16_t read_u16(const std::uint8_t* raw) {
    return static_cast<std::uint16_t>(raw[0]) |
           (static_cast<std::uint16_t>(raw[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* raw) {
    return static_cast<std::uint32_t>(raw[0]) |
           (static_cast<std::uint32_t>(raw[1]) << 8U) |
           (static_cast<std::uint32_t>(raw[2]) << 16U) |
           (static_cast<std::uint32_t>(raw[3]) << 24U);
}
```

Also remove the `#include <array>` and `#include <cstring>` includes since `memcpy` and `std::array` are no longer used.

- [ ] **Step 4: Build and run all RPC tests**

Run: `cd build-debug && cmake --build . --target tightrope-tests 2>&1 | tail -5 && ./tightrope-tests "[sync][transport][rpc]" -v 2>&1 | tail -20`
Expected: All 4 rpc_channel tests pass (2 existing + 2 new).

- [ ] **Step 5: Commit**

```
git add native/sync/src/transport/rpc_channel.cpp native/tests/unit/sync/rpc_channel_test.cpp
git commit -m "fix(sync): use explicit little-endian encoding in RPC channel framing"
```

---

### Task 6: Run Full Test Suite

- [ ] **Step 1: Build and run all sync tests**

Run: `cd build-debug && cmake --build . --target tightrope-tests 2>&1 | tail -5 && ./tightrope-tests "[sync]" -v 2>&1 | tail -50`
Expected: All sync tests pass -- existing tests are not broken by the new code.

- [ ] **Step 2: Run the full test suite**

Run: `cd build-debug && ./tightrope-tests 2>&1 | tail -10`
Expected: All tests pass. Zero failures.

- [ ] **Step 3: Commit (if any fixups were needed)**

Only if Step 1 or 2 revealed issues that required fixes:
```
git add -A
git commit -m "fix(sync): address test failures from foundation changes"
```
