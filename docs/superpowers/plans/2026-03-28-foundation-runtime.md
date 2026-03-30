# Foundation Runtime Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the current scaffold into a buildable native runtime with a real bridge lifecycle, health path, config loading, and SQLite bootstrap.

**Architecture:** Start by unblocking configure/build, then build the first vertical slice: `init()` boots config, logging, DB, and server state; `getHealth()` reports runtime status; `shutdown()` tears everything down cleanly. Keep N-API isolated in `native/bridge/` and push all real work into focused runtime modules.

**Tech Stack:** CMake, vcpkg, node-addon-api, Catch2, uWebSockets, SQLite3, SQLiteCpp, quill, toml++.

---

## Chunk 1: Build And Test Harness

### Task 1: Fix Native Configure And Link Resolution

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `build.sh`
- Modify: `CMakePresets.json`

- [x] **Step 1: Reproduce the red configure state**

Run:
```bash
./build.sh debug
```
Expected: FAIL with missing `uWebSocketsConfig.cmake` and possibly SQLite target-name mismatches.

- [x] **Step 2: Update package lookup and targets**

Normalize the CMake usage to the actual installed vcpkg package names:

- `unofficial-uwebsockets`
- `unofficial-sqlite3`
- the real NuRaft target exported by the installed port

- [x] **Step 3: Re-run configure/build**

Run:
```bash
./build.sh debug
```
Expected: CMake configure completes and produces `build-debug/tightrope-tests`.

- [x] **Step 4: Verify the current empty test harness runs**

Run:
```bash
./build-debug/tightrope-tests --reporter console
```
Expected: PASS with zero or trivial bootstrap tests.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add CMakeLists.txt build.sh CMakePresets.json
git commit -m "build: fix native configure and test harness"
```

Execution note (2026-03-28): Steps 1-4 completed. `tightrope-tests` initially returned exit code 2 (`No tests ran`), so a minimal smoke test was added at `native/tests/smoke_test.cpp` to establish a green harness.

## Chunk 2: Bridge Lifecycle And Health Slice

### Task 2: Implement Bridge Runtime State

**Files:**
- Modify: `native/bridge/bridge.h`
- Modify: `native/bridge/bridge.cpp`
- Create: `native/tests/bridge/bridge_lifecycle_test.cpp`
- Modify: `app/src/main/native.ts`

- [x] **Step 1: Write the failing bridge lifecycle test**

```cpp
TEST_CASE("bridge init toggles running state", "[bridge][lifecycle]") {
    Bridge bridge;
    REQUIRE_FALSE(bridge.is_running());
    REQUIRE(bridge.init({}));
    REQUIRE(bridge.is_running());
}
```

- [x] **Step 2: Run the test to verify it fails**

Run:
```bash
./build-debug/tightrope-tests "[bridge][lifecycle]"
```
Expected: FAIL because the bridge has no behavior yet.

- [x] **Step 3: Implement minimal bridge runtime state**

Add:

- `init(config)`
- `shutdown()`
- `is_running()`
- internal runtime state with idempotent transitions

- [x] **Step 4: Re-run the bridge lifecycle test**

Run:
```bash
./build-debug/tightrope-tests "[bridge][lifecycle]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add native/bridge/bridge.h native/bridge/bridge.cpp native/tests/bridge/bridge_lifecycle_test.cpp app/src/main/native.ts
git commit -m "feat: add bridge lifecycle state"
```

Execution note (2026-03-28): The bridge lifecycle tests passed (`[bridge][lifecycle]`) and the global test command passed after implementation.

### Task 3: Implement Health Reporting

**Files:**
- Modify: `native/server/server.h`
- Modify: `native/server/server.cpp`
- Modify: `native/server/controllers/health_controller.h`
- Modify: `native/server/controllers/health_controller.cpp`
- Create: `native/tests/server/health_controller_test.cpp`

- [x] **Step 1: Write the failing health test**

```cpp
TEST_CASE("health controller reports ok after startup", "[server][health]") {
    auto runtime = start_test_runtime();
    auto health = runtime.get_health();
    REQUIRE(health.status == "ok");
}
```

- [x] **Step 2: Run the test to verify it fails**

Run:
```bash
./build-debug/tightrope-tests "[server][health]"
```
Expected: FAIL because no health path exists yet.

- [x] **Step 3: Implement the minimal health model and controller**

Return:

- `status`
- `uptime_ms`
- optional degraded reason when startup components fail

- [x] **Step 4: Re-run the health test**

Run:
```bash
./build-debug/tightrope-tests "[server][health]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add native/server/server.h native/server/server.cpp native/server/controllers/health_controller.h native/server/controllers/health_controller.cpp native/tests/server/health_controller_test.cpp
git commit -m "feat: add health controller"
```

Execution note (2026-03-28): Added `Runtime` health model and controller adapter; `[server][health]` and full test suite both passed.

## Chunk 3: Config, Logging, And DB Bootstrap

### Task 4: Implement Config Parsing And Logger Bootstrap

**Files:**
- Modify: `native/config/config.h`
- Modify: `native/config/config_loader.h`
- Modify: `native/config/config_loader.cpp`
- Create: `native/tests/config/config_loader_test.cpp`

- [x] **Step 1: Write the failing config test**

```cpp
TEST_CASE("config loader applies explicit db path", "[config]") {
    auto config = load_config({ .db_path = "/tmp/tightrope-test.db" });
    REQUIRE(config.db_path == "/tmp/tightrope-test.db");
}
```

- [x] **Step 2: Run the test to verify it fails**

Run:
```bash
./build-debug/tightrope-tests "[config]"
```
Expected: FAIL because the config model is empty.

- [x] **Step 3: Implement config defaults and logger bootstrap**

Support:

- host
- port
- db path
- config path
- log level

- [x] **Step 4: Re-run the config test**

Run:
```bash
./build-debug/tightrope-tests "[config]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add native/config/config.h native/config/config_loader.h native/config/config_loader.cpp native/tests/config/config_loader_test.cpp
git commit -m "feat: add config loader"
```

Execution note (2026-03-28): Added `Config`, `ConfigOverrides`, and `load_config` with precedence `overrides > env > defaults`; `[config]` and full suite both passed.

### Task 5: Implement SQLite Bootstrap And Baseline Migration

**Files:**
- Modify: `native/db/connection/db_pool.h`
- Modify: `native/db/connection/sqlite_pool.h`
- Modify: `native/db/connection/sqlite_pool.cpp`
- Modify: `native/db/migration/migration_runner.h`
- Modify: `native/db/migration/migration_runner.cpp`
- Modify: `native/db/migration/integrity_check.h`
- Modify: `native/db/migration/integrity_check.cpp`
- Modify: `native/migrations/001_baseline.sql`
- Create: `native/tests/db/migration_runner_test.cpp`

- [x] **Step 1: Write the failing migration test**

```cpp
TEST_CASE("baseline migration creates schema_version", "[db][migrations]") {
    auto db = open_temp_db();
    REQUIRE(run_migrations(db).has_value());
    REQUIRE(table_exists(db, "schema_version"));
}
```

- [x] **Step 2: Run the test to verify it fails**

Run:
```bash
./build-debug/tightrope-tests "[db][migrations]"
```
Expected: FAIL because the migration runner and baseline schema are placeholders.

- [x] **Step 3: Implement pool, migration runner, and integrity check**

Port the real schema from `/Users/fabian/Development/codex-lb/app/db/alembic/versions/` into `native/migrations/001_baseline.sql`, then add startup integrity verification.

- [x] **Step 4: Re-run the migration test**

Run:
```bash
./build-debug/tightrope-tests "[db][migrations]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add native/db/connection/db_pool.h native/db/connection/sqlite_pool.h native/db/connection/sqlite_pool.cpp native/db/migration/migration_runner.h native/db/migration/migration_runner.cpp native/db/migration/integrity_check.h native/db/migration/integrity_check.cpp native/migrations/001_baseline.sql native/tests/db/migration_runner_test.cpp
git commit -m "feat: add sqlite bootstrap and baseline migration"
```

Execution note (2026-03-28): Added `DbPool`/`SqlitePool`, migration execution + table existence checks, and `PRAGMA quick_check` integrity verification. Replaced baseline migration placeholder with concrete core-table schema and indexes; `[db][migrations]` and full suite both passed.
