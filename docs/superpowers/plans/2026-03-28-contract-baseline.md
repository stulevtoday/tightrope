# Contract Baseline Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Freeze the Python service behavior into reusable HTTP, SSE, WebSocket, DB, and crypto fixtures that drive TDD for the C++ rebuild.

**Architecture:** Use `/Users/fabian/Development/codex-lb` as a read-only oracle. Capture fixtures with small scripts under `scripts/contracts/`, store them under `native/tests/contracts/fixtures/`, and replay them from Catch2 tests after the native test harness is green.

**Tech Stack:** Python reference service (`uv run fastapi run app/main.py`), shell/Python capture scripts, Catch2, JSON, NDJSON, SQLite snapshots.

**Execution note (2026-03-28):** User explicitly required **no Python execution**. Contract fixtures are being produced via static source extrapolation from `/Users/fabian/Development/codex-lb` only.

---

## Chunk 1: Capture Harness

### Task 1: Stand Up The Reference Backend Contract Harness

**Files:**
- Create: `scripts/contracts/start_reference_backend.sh`
- Create: `scripts/contracts/stop_reference_backend.sh`
- Create: `scripts/contracts/reference_env.sh`
- Create: `native/tests/contracts/reference_backend_smoke_test.cpp`

- [x] **Step 1: Write the failing smoke test**

```cpp
#include <catch2/catch_test_macros.hpp>

TEST_CASE("reference backend health endpoint is reachable", "[contracts][reference]") {
    REQUIRE_FALSE(fetch_json(std::getenv("REFERENCE_BACKEND_URL") + std::string("/health")).empty());
}
```

- [x] **Step 2: Run the test to verify the red state**

Run:
```bash
REFERENCE_BACKEND_URL=http://127.0.0.1:2460 ./build-debug/tightrope-tests "[contracts][reference]"
```
Expected: FAIL because the helper and/or reference backend are not available yet.

- [x] **Step 3: Add the reference backend helpers**

Run:
```bash
cd /Users/fabian/Development/codex-lb && uv sync
cd /Users/fabian/Development/codex-lb && uv run fastapi run app/main.py --host 127.0.0.1 --port 2460
```

Implement `scripts/contracts/start_reference_backend.sh` to launch that exact command in the background and export `REFERENCE_BACKEND_URL=http://127.0.0.1:2460`.

- [ ] **Step 4: Re-run the smoke test**
Blocked by user constraint: no Python process execution allowed.

Run:
```bash
REFERENCE_BACKEND_URL=http://127.0.0.1:2460 ./build-debug/tightrope-tests "[contracts][reference]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add scripts/contracts/start_reference_backend.sh scripts/contracts/stop_reference_backend.sh scripts/contracts/reference_env.sh native/tests/contracts/reference_backend_smoke_test.cpp
git commit -m "test: add reference backend harness"
```

### Task 2: Capture Frozen HTTP Fixtures

**Files:**
- Create: `native/tests/contracts/source_contract_catalog.{h,cpp}`
- Create: `native/tests/contracts/http_fixture_capture.{h,cpp}`
- Create: `native/tools/http_fixture_capture_main.cpp`
- Create: `scripts/contracts/capture_http_fixtures.sh`
- Create: `native/tests/contracts/fixtures/http/manifest.json`
- Create: `native/tests/contracts/http_fixture_manifest_test.cpp`

- [x] **Step 1: Write the failing manifest test**

```cpp
TEST_CASE("http fixture manifest includes critical routes", "[contracts][http]") {
    auto manifest = load_fixture_manifest("native/tests/contracts/fixtures/http/manifest.json");
    REQUIRE(manifest.contains("health"));
    REQUIRE(manifest.contains("settings_get"));
    REQUIRE(manifest.contains("models_get"));
}
```

- [x] **Step 2: Run the test to verify it fails**

Run:
```bash
./build-debug/tightrope-tests "[contracts][http]"
```
Expected: FAIL because the manifest does not exist yet.

- [x] **Step 3: Implement the capture script and manifest**

Capture at least:

- `GET /health`
- `GET /api/settings`
- `GET /api/settings/runtime/connect-address`
- `GET /api/models`
- `GET /v1/models`
- `GET /api/dashboard/overview`
- `GET /api/request-logs/options`

Write raw responses plus a manifest that records route, method, auth mode, and fixture path.

- [x] **Step 4: Run the capture and replay the manifest test**
Executed replay test only (`[contracts][http]`) using static source-extrapolated fixtures; capture script was not executed per user constraint.

Run:
```bash
scripts/contracts/capture_http_fixtures.sh
./build-debug/tightrope-tests "[contracts][http]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add scripts/contracts/capture_http_fixtures.sh native/tests/contracts/source_contract_catalog.h native/tests/contracts/source_contract_catalog.cpp native/tests/contracts/http_fixture_capture.h native/tests/contracts/http_fixture_capture.cpp native/tools/http_fixture_capture_main.cpp native/tests/contracts/fixtures/http/manifest.json native/tests/contracts/http_fixture_manifest_test.cpp
git commit -m "test: capture frozen http fixtures"
```

### Task 3: Capture Golden Streaming Fixtures

**Files:**
- Create: `native/tests/contracts/streaming_fixture_capture.{h,cpp}`
- Create: `native/tools/streaming_fixture_capture_main.cpp`
- Create: `scripts/contracts/capture_streaming_fixtures.sh`
- Create: `native/tests/contracts/fixtures/streaming/responses_sse.ndjson`
- Create: `native/tests/contracts/fixtures/streaming/responses_ws.ndjson`
- Create: `native/tests/contracts/streaming_fixture_test.cpp`
- Create: `native/tests/contracts/streaming_fixture_capture_test.cpp`
- Modify: `CMakeLists.txt`

- [x] **Step 1: Write the failing streaming fixture test**

```cpp
TEST_CASE("streaming fixtures preserve ordered event transcripts", "[contracts][streaming]") {
    auto sse_events = load_ndjson("native/tests/contracts/fixtures/streaming/responses_sse.ndjson");
    REQUIRE(sse_events.size() > 3);
}
```

- [x] **Step 2: Run the test to verify it fails**

Run:
```bash
./build-debug/tightrope-tests "[contracts][streaming]"
```
Expected: FAIL because the transcripts are not present yet.

- [x] **Step 3: Capture SSE and WebSocket transcripts**
Implemented as tightrope-native static NDJSON contracts derived from source behavior, not from runtime Python execution.

Record:

- `/backend-api/codex/responses`
- `/v1/responses`
- WebSocket equivalents for the same flows
- event order
- event payload
- terminal event and close code semantics

- [x] **Step 4: Re-run the streaming fixture test**
Executed Catch2 replay only (`[contracts][streaming]`) with static fixtures.

Run:
```bash
scripts/contracts/capture_streaming_fixtures.sh
./build-debug/tightrope-tests "[contracts][streaming]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add scripts/contracts/capture_streaming_fixtures.sh native/tests/contracts/streaming_fixture_capture.h native/tests/contracts/streaming_fixture_capture.cpp native/tools/streaming_fixture_capture_main.cpp native/tests/contracts/fixtures/streaming native/tests/contracts/streaming_fixture_test.cpp native/tests/contracts/streaming_fixture_capture_test.cpp CMakeLists.txt
git commit -m "test: capture golden streaming fixtures"
```

## Chunk 2: Replay Harness

### Task 4: Add C++ Replay Helpers For Frozen Fixtures

**Files:**
- Create: `native/tests/contracts/fixture_loader.h`
- Create: `native/tests/contracts/fixture_loader.cpp`
- Create: `native/tests/contracts/http_contract_replay_test.cpp`
- Modify: `CMakeLists.txt`

- [x] **Step 1: Write the failing replay test**

```cpp
TEST_CASE("health contract replay returns expected payload shape", "[contracts][replay]") {
    auto fixture = load_http_fixture("health");
    REQUIRE(fixture.response.status == 200);
    REQUIRE(fixture.response.body.contains("\"status\""));
}
```

- [x] **Step 2: Run the test to verify it fails**

Run:
```bash
./build-debug/tightrope-tests "[contracts][replay]"
```
Expected: FAIL because the loader is missing.

- [x] **Step 3: Implement the fixture loader and replay helper**

Support:

- JSON fixtures
- NDJSON fixtures
- future DB/crypto fixture descriptors

- [x] **Step 4: Re-run the replay test**

Run:
```bash
./build-debug/tightrope-tests "[contracts][replay]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add native/tests/contracts/fixture_loader.h native/tests/contracts/fixture_loader.cpp native/tests/contracts/http_contract_replay_test.cpp CMakeLists.txt
git commit -m "test: add contract replay helpers"
```

### Task 5: Add DB And Crypto Compatibility Fixtures

**Files:**
- Create: `scripts/contracts/capture_compatibility_fixtures.py`
- Create: `native/tests/contracts/fixtures/db/legacy_store.db`
- Create: `native/tests/contracts/fixtures/crypto/encryption.key`
- Create: `native/tests/contracts/fixtures/crypto/token_blob.txt`
- Create: `native/tests/contracts/compatibility_fixture_test.cpp`

- [x] **Step 1: Write the failing compatibility fixture test**

```cpp
TEST_CASE("legacy db and crypto fixtures are present", "[contracts][compat]") {
    REQUIRE(path_exists("native/tests/contracts/fixtures/db/legacy_store.db"));
    REQUIRE(path_exists("native/tests/contracts/fixtures/crypto/encryption.key"));
}
```

- [x] **Step 2: Run the test to verify it fails**

Run:
```bash
./build-debug/tightrope-tests "[contracts][compat]"
```
Expected: FAIL because the fixtures are not present yet.

- [x] **Step 3: Capture sanitized compatibility fixtures from the Python reference**
Implemented as tightrope-owned static compatibility fixtures without runtime Python execution.

Include:

- a migrated SQLite snapshot
- a representative encryption key
- encrypted token blobs readable by the existing Python service

- [x] **Step 4: Re-run the compatibility fixture test**

Run:
```bash
python3 scripts/contracts/capture_compatibility_fixtures.py
./build-debug/tightrope-tests "[contracts][compat]"
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add scripts/contracts/capture_compatibility_fixtures.py native/tests/contracts/fixtures native/tests/contracts/compatibility_fixture_test.cpp
git commit -m "test: add db and crypto compatibility fixtures"
```
