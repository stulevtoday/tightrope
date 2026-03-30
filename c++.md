# tightrope C++ Rebuild Blueprint (Electron Frontend, No Docker)

Last updated: 2026-03-28
Scope analyzed:
- Python reference implementation in `/Users/fabian/Development/codex-lb`
- Current C++ rebuild status in `/Users/fabian/Development/tightrope`

## 1. Goal

Rebuild the current Python/FastAPI + React web app into:

- C++ backend (production runtime and core logic)
- Electron desktop frontend (existing React UI migrated into Electron renderer)
- No Docker dependency for dev, build, or production usage

Primary requirement: preserve user-visible behavior and API compatibility while improving runtime control/performance and desktop distribution.

## 1.1 Current Implementation Status (2026-03-28)

This section turns the blueprint into a working checklist. Status below is based on the current `tightrope` repo only; Python files in `codex-lb` remain reference material, not implementation work to modify.

Current evidence:

- `./build.sh app` succeeds, so the Electron TypeScript shell compiles.
- `./build.sh debug` now configures and builds successfully with vcpkg package resolution (`unofficial-uwebsockets`, SQLite, Boost components).
- Native sync foundations are implemented and tested: HLC, checksums, journal/CDC, sync protocol/engine, Raft primitives, CRDT primitives + persistence + merge sync, TLS/LZ4/RPC channel, and discovery peer management.
- `native/tests/` now includes unit, integration, and contract fixtures with active Catch2 coverage (`./build-debug/tightrope-tests` passing `1314` assertions across `133` test cases).
- Remaining large gaps are now admin API migration, full HTTP server bootstrap, and CI automation.

### Status-Tracked Execution Checklist

#### Phase 0: Contract Freeze and Test Harness

- [x] Capture frozen JSON fixtures from codex-lb source contracts for critical admin and proxy endpoints (no Python execution).
- [x] Capture golden SSE event streams for `/backend-api/codex/responses` and `/v1/responses` via source-derived fixtures.
- [x] Capture golden websocket transcripts for `/backend-api/codex/responses` and `/v1/responses` via source-derived fixtures.
- [ ] Record baseline latency and throughput snapshots from the Python service.
- [x] Create contract replay tests in C++ that assert byte-level or field-level compatibility against frozen fixtures.

#### Phase 1: C++ Skeleton + Infra

- [x] Create the top-level CMake project, presets, triplets, `vcpkg.json`, and `build.sh`.
- [x] Reserve the native domain-oriented folder structure under `native/`.
- [x] Add the Electron main/preload shell plus a typed native wrapper in `app/src/main` and `app/src/preload`.
- [x] Vendor `vendor/mdns.h` for the future discovery layer.
- [x] Fix CMake package discovery and linking so `./build.sh debug` configures and builds successfully.
- [x] Implement a real N-API bridge with `init`, `shutdown`, `isRunning`, and `getHealth` exports.
- [x] Implement config parsing.
- [ ] Implement application-wide logger bootstrap (beyond consensus logging).
- [ ] Implement the HTTP server bootstrap and route wiring.
- [x] Implement `/health` controller endpoint.
- [x] Implement SQLite pool, migration runner, and integrity checks.
- [x] Replace the placeholder baseline migration with the real schema port.
- [x] Add real unit and integration test targets beyond the Catch2 bootstrap.
- [ ] Add CI build and test automation.

#### Phase 2: Admin APIs First

- [x] Port `dashboard_auth` password verification, TOTP verification, and session management.
- [x] Port `settings` persistence and controller behavior.
- [x] Port `accounts` persistence and controller behavior.
- [x] Port `api_keys` validation, limits, reservations, and controllers.
- [ ] Port `firewall` persistence and controller behavior.
- [ ] Port `sticky_sessions` persistence and controller behavior.
- [ ] Port `request_logs` queries, options, and filters.
- [ ] Port `usage` and dashboard overview queries.
- [ ] Replace the sketch placeholder with the migrated React renderer.
- [ ] Point Electron admin workflows at the C++ backend and verify parity.

#### Phase 3: Proxy Core (SSE + compact + transcribe)

- [x] Implement OpenAI payload normalization and error envelope compatibility.
- [x] Implement `POST /backend-api/codex/responses` and `POST /v1/responses`.
- [x] Implement `POST /backend-api/codex/responses/compact` and `POST /v1/responses/compact`.
- [x] Implement `POST /backend-api/transcribe` and `POST /v1/audio/transcriptions`.
- [x] Implement `GET /backend-api/codex/models`, `GET /v1/models`, and model capability lookup.
- [x] Add contract replay coverage for JSON proxy responses.
- [x] Add golden SSE approval tests for event order and payload content.

#### Phase 4: WebSocket Bridge + Sticky Affinity

- [x] Implement websocket proxy endpoints for both compatibility paths.
- [x] Implement sticky session key resolution for codex sessions, threads, and prompt cache.
- [x] Persist sticky mappings in SQLite and wire cleanup scheduling.
- [ ] Implement HTTP bridge session behavior where configured.
- [x] Add websocket integration tests and client compatibility checks.

#### Phase 5: Hardening and Performance

- [ ] Add structured logging and observability metrics.
- [ ] Implement cancellation handling and error-path resilience.
- [ ] Tune request-log and usage queries, indexes, and pagination paths.
- [ ] Add long-run soak tests.
- [ ] Demonstrate equal-or-better p95 latency on representative workloads.
- [ ] Verify backward compatibility for existing `store.db` and `encryption.key` data.

#### Phase 6: Cutover

- [ ] Produce production desktop builds for macOS, Linux, and Windows.
- [ ] Write migration, packaging, and rollback documentation.
- [ ] Run pilot validation on real workloads.
- [ ] Complete a rollback drill.

#### Cross-Cutting Sync / Cluster Track

- [x] Reserve the sync/consensus/CRDT/transport/discovery file layout under `native/sync/`.
- [x] Implement HLC, journal persistence, checksums, and CDC trigger generation.
- [x] Implement sync protocol encoding/decoding and batch exchange.
- [x] Implement conflict resolution rules per replicated table.
- [x] Implement Raft node, log, membership, RPC, and leader forwarding.
- [x] Implement CRDT primitives, persistence, and merge logic.
- [x] Implement TLS, LZ4, RPC channel, mDNS publish/browse, and peer management.
- [x] Expose cluster and sync bridge APIs to Electron.

#### Immediate Next Tasks

- [x] Fix the native build blockers in `CMakeLists.txt` (`uWebSockets` and SQLite package wiring).
- [x] Normalize the C++ namespace convention on `tightrope::...`.
- [x] Replace health/config/DB placeholder stubs with a first working vertical slice.
- [x] Port the baseline schema into `native/migrations/001_baseline.sql`.
- [x] Build the contract-fixture capture harness against `/Users/fabian/Development/codex-lb` source contracts (no Python execution).
- [x] Implement SQLite-backed sticky/session bridge persistence and wire it into proxy flows.
- [x] Port `dashboard_auth`, `settings`, and `accounts` modules with repository-backed controllers.
- [ ] Add CI build/test automation.

## 2. Current Codebase Analysis

## 2.1 Size and Hotspots

- Backend Python files: `185`
- Frontend TS/TSX files: `185`
- Backend tests: `93` (unit + integration)
- Frontend tests: `54` (+ `5` integration flow tests)
- DB migrations: `39` Alembic revisions
- Backend LOC (`app/**/*.py`): `30,501`
- Frontend LOC (`frontend/src/**/*.{ts,tsx}`): `18,458`

Largest backend files (highest migration risk):

- `app/modules/proxy/service.py` (`5210` LOC)
- `app/core/clients/proxy.py` (`2040` LOC)
- `app/modules/api_keys/service.py` (`1007` LOC)
- `app/modules/proxy/api.py` (`986` LOC)
- `app/modules/proxy/load_balancer.py` (`953` LOC)

## 2.2 Backend Architecture (Current)

Runtime entry:

- `app/main.py`
- Lifespan initializes DB, HTTP client, schedulers, middleware, routers, SPA fallback

Domain modules:

- `accounts` (import/pause/reactivate/delete, trends)
- `oauth` (browser/device flow + callback server)
- `proxy` (Responses API, streaming SSE, websocket proxy, compact, transcribe)
- `dashboard` + `usage` + `request_logs`
- `settings` + `dashboard_auth` (password + TOTP)
- `api_keys` (limits, enforcement, reservation settlement)
- `firewall` (allowlist)
- `sticky_sessions` (affinity mappings + purge/cleanup)

Cross-cutting core:

- `core/openai/*` (request/response normalization and compatibility)
- `core/balancer/*` (selection strategy logic)
- `core/middleware/*` (decompression, request-id, firewall)
- `core/usage/*` (quota/cost/depletion)
- `db/*` (models, migrations, startup checks)

## 2.3 API Surface (Current Contracts)

Proxy/OpenAI-compatible endpoints:

- `POST /backend-api/codex/responses` (SSE stream)
- `WS /backend-api/codex/responses`
- `POST /v1/responses` (SSE or JSON)
- `WS /v1/responses`
- `POST /v1/chat/completions`
- `POST /backend-api/codex/responses/compact`
- `POST /v1/responses/compact`
- `POST /backend-api/transcribe`
- `POST /v1/audio/transcriptions`
- `GET /backend-api/codex/models`
- `GET /v1/models`
- `GET /api/codex/usage`

Dashboard/Admin endpoints:

- `GET /api/dashboard/overview`
- `GET /api/models`
- `GET/POST/... /api/accounts*`
- `GET/PUT /api/settings`
- `GET /api/settings/runtime/connect-address`
- `POST/GET /api/oauth/*`
- `GET/POST/DELETE /api/dashboard-auth/*`
- `GET/POST/PATCH/DELETE /api/api-keys/*`
- `GET /api/request-logs` and `/api/request-logs/options`
- `GET/POST/DELETE /api/firewall/ips*`
- `GET/POST/DELETE /api/sticky-sessions*`
- `GET /health`

## 2.4 Data Model (Current)

Core tables:

- `accounts`
- `usage_history`
- `additional_usage_history`
- `request_logs`
- `sticky_sessions`
- `dashboard_settings`
- `api_firewall_allowlist`
- `api_keys`
- `api_key_limits`
- `api_key_usage_reservations`
- `api_key_usage_reservation_items`

Database engine: SQLite (sole engine — PostgreSQL support removed).

## 2.5 Critical Behaviors to Preserve

- Streaming compatibility for both SSE and websocket flows
- Sticky session affinity for codex sessions, threads, and prompt cache
- Load-balancer behavior (`usage_weighted` and `round_robin`) with cooldown/error backoff
- API key auth and per-key model/reasoning/limit enforcement
- Dashboard password + optional TOTP session semantics
- Request log semantics and filtering facets
- Usage refresh/model refresh background loops
- Existing encrypted token readability (`Fernet` key file behavior)

## 2.6 Frontend (Current)

- React 19 + Vite + Tailwind + TanStack Query + Zustand + Zod
- Feature-organized frontend under `frontend/src/features/*`
- API clients already centralized in `frontend/src/features/*/api.ts` and `frontend/src/lib/api-client.ts`

This is favorable for Electron migration because renderer code can mostly be reused.

## 3. Rebuild Principles

- Preserve existing API contracts first, optimize later
- Keep one clear runtime path: native backend as Node native module + Electron app, no containers
- Keep backend deployable headless (CLI mode) and embedded (Electron-managed mode)
- Migrate in phases with measurable parity gates

### 3.1 Code Quality Mandates

**Template-style programming**: Use C++ templates and compile-time polymorphism where it reduces boilerplate and improves type safety. Prefer `template<typename T>` over runtime virtual dispatch for hot paths (scoring, serialization, request dispatch). Reserve virtual dispatch for plugin-style extension points only.

**Maximum file size**: No single `.cpp` or `.h` file should exceed **400 LOC** (excluding generated code and test fixtures). If a file approaches this limit, split it. The current Python codebase has files over 5000 LOC — this is explicitly not acceptable in the rebuild.

**Modular design**: Every module must be independently compilable and testable. No circular header dependencies. Use forward declarations aggressively. Each module exposes a clean header interface and hides implementation details.

**No monolithic files**: A single class should not span multiple responsibilities. Prefer many small, focused files over few large ones. Split by concern:
- One header + one implementation per logical unit (service, controller, model, utility)
- Separate request parsing, business logic, and response formatting
- Separate SQL query construction from result mapping

**Proper folder structure**: Code is organized by domain, not by file type. Each domain folder contains its own headers, implementations, and tests. See section 5.2 for the full layout.

**Naming conventions**:
- Files: `snake_case.h`, `snake_case.cpp`
- Classes/structs: `PascalCase`
- Functions/methods: `snake_case`
- Constants: `kPascalCase`
- Template parameters: `PascalCase`
- Namespaces: `codexlb::<module>`

## 4. Library Stack

All dependencies must be statically linkable with permissive licenses (no GPL/AGPL/LGPL). Package management via **vcpkg** in manifest mode (`vcpkg.json`). All vcpkg ports built as static libraries (`VCPKG_LIBRARY_LINKAGE=static`).

### 4.1 Recommended Libraries

| # | Domain | Library | License | Embedding | Rationale |
|---|--------|---------|---------|-----------|-----------|
| 1 | **Raft consensus** | [NuRaft](https://github.com/eBay/NuRaft) | Apache-2.0 | vcpkg: `nuraft` | Pluggable state machine, log store, and RPC layer; used in production at eBay/JunoDB. Only actively maintained embeddable Raft library. |
| 2 | **HTTP/WS server** | [uWebSockets](https://github.com/uNetworking/uWebSockets) | Apache-2.0 | vcpkg: `uwebsockets` | Fastest C++ HTTP/WebSocket server; native SSE streaming support; minimal footprint ideal for proxy hot path. |
| 3 | **TLS** | [mbedTLS](https://github.com/Mbed-TLS/mbedtls) | Apache-2.0 | vcpkg: `mbedtls` | Purpose-built for static embedding; modular config strips unused ciphers; zero external dependencies. Fallback: OpenSSL if complex cert chain support is needed. |
| 4 | **SQLite** | [SQLite amalgamation](https://sqlite.org/amalgamation.html) + [SQLiteCpp](https://github.com/SRombauts/SQLiteCpp) | Public Domain + MIT | vcpkg: `sqlite3` + `sqlitecpp` | Canonical embedding method; SQLiteCpp adds RAII `Database`, `Statement`, `Transaction` wrappers without abstraction leak. |
| 5 | **JSON** | [glaze](https://github.com/stephenberry/glaze) | MIT | vcpkg: `glaze` | Fastest JSON serialization + deserialization via compile-time reflection; eliminates boilerplate. Alternative: [nlohmann/json](https://github.com/nlohmann/json) (MIT, vcpkg: `nlohmann-json`) if C++17 is required — slower but universally understood. |
| 6 | **Compression** | [LZ4](https://github.com/lz4/lz4) | BSD-2-Clause | vcpkg: `lz4` | Industry standard real-time compression; fastest decompression of any algorithm; used by Linux kernel, Facebook, Apple. |
| 7 | **Cryptography** | [libsodium](https://github.com/jedisct1/libsodium) | ISC | vcpkg: `libsodium` | Misuse-resistant API for authenticated encryption (AES-256-GCM / XChaCha20-Poly1305), SHA-256, HMAC, key derivation. Covers Fernet-compatible encrypt/decrypt. For TOTP (HMAC-SHA1): use mbedTLS `mbedtls_md_hmac()` since it's already in the stack. |
| 8 | **mDNS/DNS-SD** | [mjansson/mdns](https://github.com/mjansson/mdns) | Unlicense | Vendored: `vendor/mdns.h` | Single-header, not in vcpkg. Cross-platform mDNS service publish + browse with zero dependencies; no daemon required (unlike Avahi). |
| 9 | **Logging** | [quill](https://github.com/odygrd/quill) | MIT | vcpkg: `quill` | Single-digit nanosecond hot path via lock-free SPSC queue; structured logging with JSON output; 2-5x faster than spdlog on hot path. Alternative: [spdlog](https://github.com/gabime/spdlog) (MIT, vcpkg: `spdlog`) for ecosystem familiarity. |
| 10 | **Testing** | [Catch2 v3](https://github.com/catchorg/Catch2) | BSL-1.0 | vcpkg: `catch2` | BDD-style test cases, expression decomposition, built-in benchmarking, tag-based selection. Alternative: GoogleTest if advanced mocking needed. |
| 11 | **Async / event loop** | [libuv](https://github.com/libuv/libuv) | MIT | vcpkg: `libuv` | Powers Node.js itself — native addon shares the same event loop with zero bridging overhead. Natural choice for a Node.js native module. |
| 12 | **Node addon** | [node-addon-api](https://github.com/nodejs/node-addon-api) | MIT | npm: `node-addon-api` | Official N-API C++ wrapper; ABI-stable across Node.js versions (14-22+); RAII and type-safe. Comes via npm, not vcpkg. Build via [cmake-js](https://github.com/nicknisi/cmake-js) for CMake integration. |
| 13 | **Configuration** | [toml++](https://github.com/marzer/tomlplusplus) | MIT | vcpkg: `tomlplusplus` | Full TOML v1.0 compliant; excellent error messages with source positions. Use TOML for config files, JSON for wire format. |
| 14 | **CRDT** | Hand-roll | N/A | ~500 LOC in `native/sync/crdt/` | No production-grade C++ CRDT library exists. G-Counter, PN-Counter, LWW-Register, OR-Set are each 50-200 lines. Reference: [delta-enabled-crdts](https://github.com/CBaquero/delta-enabled-crdts) for algorithms. |
| 15 | **Build / packages** | CMake 3.21+ + [vcpkg](https://github.com/microsoft/vcpkg) | BSD-3 + MIT | Manifest mode (`vcpkg.json`) | Industry-standard C++ package manager; manifest mode pins versions; triplet controls static linking; integrates with CMake via toolchain file. |

### 4.2 Dependency Graph

```
tightrope-core.node
├── node-addon-api (header-only, N-API bridge)
├── libuv (static, event loop — shared with Node.js runtime)
├── uWebSockets (static, HTTP/WS proxy server)
│   └── uSockets (static, low-level sockets)
│       └── mbedTLS (static, TLS 1.3)
├── SQLite amalgamation (compiled-in, database)
│   └── SQLiteCpp (static, C++ wrapper)
├── NuRaft (static, Raft consensus for cluster sync)
├── libsodium (static, Fernet encryption + SHA-256 + HMAC)
├── glaze (header-only, JSON serialization)
├── LZ4 (compiled-in, sync batch compression)
├── mjansson/mdns (header-only, peer auto-discovery)
├── toml++ (header-only, config file parsing)
├── quill (static, structured logging)
└── [test-only]
    └── Catch2 v3 (static, test framework)
```

### 4.3 vcpkg Manifest

```json
{
  "name": "tightrope-core",
  "version-semver": "0.1.0",
  "dependencies": [
    { "name": "nuraft",        "version>=": "2.3.0" },
    { "name": "uwebsockets",   "version>=": "20.70.0" },
    { "name": "mbedtls",       "version>=": "3.6.0" },
    { "name": "sqlite3",       "version>=": "3.46.0",
      "features": ["json1", "fts5"] },
    { "name": "sqlitecpp",     "version>=": "3.3.2" },
    { "name": "glaze",         "version>=": "4.0.0" },
    { "name": "lz4",           "version>=": "1.10.0" },
    { "name": "libsodium",     "version>=": "1.0.20" },
    { "name": "quill",         "version>=": "7.0.0" },
    { "name": "tomlplusplus",  "version>=": "3.4.0" },
    { "name": "libuv",         "version>=": "1.49.0" }
  ],
  "dev-dependencies": [
    { "name": "catch2",        "version>=": "3.7.0" }
  ],
  "builtin-baseline": "<pin to latest vcpkg commit>",
  "overrides": []
}
```

### 4.4 vcpkg Triplet (Static Linking)

Create `triplets/community/x64-<platform>-static-release.cmake` or use built-in static triplets:

```cmake
# triplets/x64-osx-static.cmake  (example for macOS)
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)          # system CRT always dynamic on macOS/Linux
set(VCPKG_LIBRARY_LINKAGE static)        # all vcpkg libs link statically
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_BUILD_TYPE release)            # skip debug builds for faster CI
```

Use triplet via: `cmake -DVCPKG_TARGET_TRIPLET=x64-osx-static ..`

### 4.5 CMakeLists.txt Skeleton

```cmake
cmake_minimum_required(VERSION 3.21)
project(tightrope-core LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

# vcpkg integrates via CMAKE_TOOLCHAIN_FILE (set by cmake-js or CLI)
# cmake-js sets this automatically when VCPKG_ROOT is set

# Find vcpkg-managed packages
find_package(nuraft CONFIG REQUIRED)
find_package(uWebSockets CONFIG REQUIRED)
find_package(MbedTLS CONFIG REQUIRED)
find_package(SQLite3 CONFIG REQUIRED)
find_package(SQLiteCpp CONFIG REQUIRED)
find_package(glaze CONFIG REQUIRED)
find_package(lz4 CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(quill CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)

add_library(tightrope-core SHARED
  bridge/init.cpp
  bridge/exports.cpp
  # ... all native sources
)

target_include_directories(tightrope-core PRIVATE
  ${CMAKE_JS_INC}                        # node-addon-api (from npm)
  ${CMAKE_SOURCE_DIR}/vendor             # mjansson/mdns.h (vendored)
)

target_link_libraries(tightrope-core PRIVATE
  NuRaft::nuraft
  uWebSockets::uWebSockets
  MbedTLS::mbedtls MbedTLS::mbedcrypto MbedTLS::mbedx509
  SQLite::SQLite3
  SQLiteCpp
  glaze::glaze
  lz4::lz4
  unofficial-sodium::sodium
  quill::quill
  tomlplusplus::tomlplusplus
  $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
  ${CMAKE_JS_LIB}
)

set_target_properties(tightrope-core PROPERTIES
  PREFIX "" SUFFIX ".node"
)

# Tests
if(BUILD_TESTING)
  find_package(Catch2 CONFIG REQUIRED)
  add_executable(tightrope-tests
    tests/main.cpp
    # ... test sources
  )
  target_link_libraries(tightrope-tests PRIVATE Catch2::Catch2WithMain)
endif()
```

### 4.6 Build Commands

```bash
# Prerequisites (one-time)
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=$(pwd)/vcpkg

# Install deps + build (cmake-js handles toolchain wiring)
npm install                              # gets node-addon-api + cmake-js
npx cmake-js build \
  --CDVCPKG_TARGET_TRIPLET=x64-osx-static \
  --CDCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake

# Or raw CMake
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-osx-static
cmake --build build
```

### 4.7 Vendored Dependencies

Libraries not available in vcpkg are vendored directly:

| Library | File | Reason |
|---------|------|--------|
| mjansson/mdns | `vendor/mdns.h` | Single header, not in vcpkg registry. Copy from [release](https://github.com/mjansson/mdns/releases). |
| CRDT types | `native/sync/crdt/*.hpp` | Hand-rolled (~500 LOC), project-specific merge semantics. |

### 4.8 License Audit

All libraries in the stack use permissive licenses compatible with commercial distribution:

| License | Libraries |
|---------|-----------|
| Apache-2.0 | NuRaft, uWebSockets, mbedTLS |
| MIT | SQLiteCpp, glaze, libsodium (ISC ≈ MIT), quill, toml++, node-addon-api, libuv, vcpkg |
| BSD-2-Clause | LZ4 (lib only — CLI is GPL but not needed) |
| BSL-1.0 | Catch2 |
| Unlicense | mjansson/mdns |
| Public Domain | SQLite amalgamation |

No GPL, AGPL, or LGPL in the dependency tree. Safe for closed-source distribution.

## 5. Target Architecture

## 5.1 Process Model

The C++ backend is compiled as a **Node.js native addon** (N-API/node-addon-api) loaded directly into the Electron main process. No child process, no HTTP loopback for internal communication.

Desktop mode (default):

- Electron main process loads `tightrope-core.node` native module
- Native module exposes a bridge API to JavaScript: `init()`, `shutdown()`, `getHealth()`, config accessors
- The native module runs its own HTTP/WebSocket server for external proxy clients (binds `127.0.0.1:<port>`)
- Electron renderer communicates with main process via IPC; main process calls native module directly
- Lifecycle management (start, health-check, restart, graceful stop) happens in-process — no PID management or crash detection needed

Headless mode (optional):

- `node -e "require('./tightrope-core.node').startStandalone({host:'127.0.0.1',port:2455})"`
- Or a thin CLI wrapper: `tightrope --host 127.0.0.1 --port 2455`
- Useful for server/CLI deployments without Electron

### 5.1.1 Native Module Bridge

The bridge layer (`bridge/`) is the only code that touches N-API. It translates between JavaScript and C++:

```
Electron renderer  ←IPC→  Electron main (JS)  ←N-API→  C++ native module
                                                           ↕
External clients   ←HTTP/WS→  C++ HTTP server (runs inside native module)
```

The bridge exposes:
- Lifecycle: `init(config)`, `shutdown()`, `isRunning()`, `getHealth()`
- Config: `getSettings()`, `updateSettings(patch)`
- Dashboard data: `getAccounts()`, `getRequestLogs(query)`, `getSessions(query)`
- Actions: `importAccount(data)`, `createApiKey(params)`, etc.

All bridge functions return promises (async N-API). The C++ side uses a thread pool; bridge calls are non-blocking.

## 5.2 Repository Layout

Each domain folder is self-contained: headers, implementations, and tests live together. No file exceeds 400 LOC.

```text
tightrope/
  native/                           # All C++ code lives here
    CMakeLists.txt
    binding.gyp                     # node-gyp/cmake-js build config

    bridge/                         # N-API boundary (the ONLY code touching node-addon-api)
      bridge.h                      # Bridge class declaration
      bridge.cpp                    # Module init, lifecycle exports
      bridge_accounts.cpp           # Account-related bridge methods
      bridge_settings.cpp           # Settings bridge methods
      bridge_logs.cpp               # Request logs bridge methods
      bridge_sessions.cpp           # Sticky sessions bridge methods
      bridge_keys.cpp               # API key bridge methods
      bridge_helpers.h              # N-API ↔ C++ type conversions

    core/                           # Shared types, utilities, no business logic
      types/
        account.h                   # Account struct
        request.h                   # Request/response structs
        session.h                   # StickySession struct
        api_key.h                   # ApiKey, ApiKeyLimit structs
        settings.h                  # DashboardSettings struct
        result.h                    # Result<T, E> template
      json/
        serialize.h                 # nlohmann-based to/from JSON templates
        serialize.cpp
      time/
        clock.h                     # Mockable clock abstraction
        ewma.h                      # EWMA template (used by balancer + usage)
      error/
        error_codes.h               # Enum of domain error codes
        app_error.h                 # Error type with code + message

    config/
      config.h                      # Configuration struct (from env/file)
      config_loader.h               # Parse env vars and .env files
      config_loader.cpp

    db/                             # Database access layer
      connection/
        db_pool.h                   # Connection pool interface
        sqlite_pool.h               # SQLite connection pool
        sqlite_pool.cpp
      migration/
        migration_runner.h          # Apply SQL migrations
        migration_runner.cpp
        integrity_check.h           # SQLite integrity checks
        integrity_check.cpp
      repositories/
        account_repo.h              # Account CRUD
        account_repo.cpp
        session_repo.h              # StickySession CRUD
        session_repo.cpp
        settings_repo.h             # DashboardSettings CRUD
        settings_repo.cpp
        request_log_repo.h          # Request log queries
        request_log_repo.cpp
        api_key_repo.h              # API key + limits CRUD
        api_key_repo.cpp
        firewall_repo.h             # IP allowlist CRUD
        firewall_repo.cpp
        usage_repo.h                # Usage history queries
        usage_repo.cpp

    balancer/                       # Load balancing strategies
      scorer.h                      # CompositeScorer template
      scorer.cpp
      strategies/
        strategy.h                  # Strategy interface (template concept)
        power_of_two.h              # P2C selection
        power_of_two.cpp
        round_robin.h
        round_robin.cpp
        weighted_round_robin.h
        weighted_round_robin.cpp
        least_outstanding.h
        least_outstanding.cpp
        latency_ewma.h
        latency_ewma.cpp
        success_rate.h
        success_rate.cpp
        headroom.h
        headroom.cpp
        cost_aware.h
        cost_aware.cpp
        deadline_aware.h
        deadline_aware.cpp
      eligibility.h                 # Account eligibility checks
      eligibility.cpp

    proxy/                          # Upstream proxy logic
      proxy_service.h
      proxy_service.cpp
      stream/
        sse_handler.h               # SSE stream proxying
        sse_handler.cpp
        ws_handler.h                # WebSocket proxy
        ws_handler.cpp
        compact_handler.h           # Compact/summarize requests
        compact_handler.cpp
        transcribe_handler.h        # Audio transcription proxy
        transcribe_handler.cpp
      openai/
        payload_normalizer.h        # Request/response shape normalization
        payload_normalizer.cpp
        error_envelope.h            # OpenAI-compatible error formatting
        error_envelope.cpp
        model_registry.h            # Model capability lookups
        model_registry.cpp
      session/
        sticky_resolver.h           # Sticky session key resolution
        sticky_resolver.cpp
        session_bridge.h            # HTTP responses session bridge
        session_bridge.cpp
        session_cleanup.h           # Stale session purge scheduler
        session_cleanup.cpp

    auth/                           # Authentication and authorization
      crypto/
        fernet.h                    # Fernet-compatible encrypt/decrypt
        fernet.cpp
        key_file.h                  # Encryption key file I/O
        key_file.cpp
      oauth/
        oauth_service.h             # PKCE + device code flows
        oauth_service.cpp
        callback_server.h           # Local OAuth callback HTTP server
        callback_server.cpp
        token_refresh.h             # Background token refresh
        token_refresh.cpp
      dashboard/
        password_auth.h             # bcrypt password verification
        password_auth.cpp
        totp_auth.h                 # TOTP verification
        totp_auth.cpp
        session_manager.h           # Cookie session management
        session_manager.cpp
      api_keys/
        key_validator.h             # sk-clb-... key validation + hashing
        key_validator.cpp
        limit_enforcer.h            # Per-key limit checking
        limit_enforcer.cpp
        reservation.h               # Usage reservation/settlement
        reservation.cpp

    usage/                          # Usage tracking and refresh
      usage_fetcher.h               # Fetch usage from upstream
      usage_fetcher.cpp
      usage_updater.h               # Background usage refresh loop
      usage_updater.cpp
      cost_calculator.h             # Per-request cost estimation
      cost_calculator.cpp
      trend_aggregator.h            # 7-day usage trend computation
      trend_aggregator.cpp

    server/                         # HTTP server (external-facing)
      server.h                      # Drogon server setup
      server.cpp
      middleware/
        request_id.h                # Request ID injection
        request_id.cpp
        decompression.h             # Body decompression
        decompression.cpp
        firewall_filter.h           # IP allowlist enforcement
        firewall_filter.cpp
        api_key_filter.h            # API key auth enforcement
        api_key_filter.cpp
      controllers/
        health_controller.h
        health_controller.cpp
        proxy_controller.h          # /backend-api/*, /v1/* routes
        proxy_controller.cpp
        accounts_controller.h       # /api/accounts/*
        accounts_controller.cpp
        settings_controller.h       # /api/settings/*
        settings_controller.cpp
        auth_controller.h           # /api/dashboard-auth/*
        auth_controller.cpp
        keys_controller.h           # /api/api-keys/*
        keys_controller.cpp
        logs_controller.h           # /api/request-logs/*
        logs_controller.cpp
        sessions_controller.h       # /api/sticky-sessions/*
        sessions_controller.cpp
        firewall_controller.h       # /api/firewall/*
        firewall_controller.cpp
        oauth_controller.h          # /api/oauth/*
        oauth_controller.cpp
        usage_controller.h          # /api/codex/usage, /api/dashboard/overview
        usage_controller.cpp

    migrations/                        # Ordered SQL migration files

    tests/
      unit/
        balancer/                   # One test file per strategy
        proxy/
        auth/
        db/
        usage/
      integration/
        api/                        # End-to-end API tests
        streaming/                  # SSE + WebSocket golden tests
      contracts/                    # Frozen request/response fixtures
      fixtures/                     # Shared test data

  app/                              # Electron application
    package.json
    src/
      main/
        index.ts                    # Electron main process
        native.ts                   # Native module loader + typed wrapper
        ipc.ts                      # IPC handlers (renderer ↔ main ↔ native)
      preload/
        index.ts                    # Minimal preload API
      renderer/                     # React app (migrated from frontend/src)
        ...
```

## 6. Module Migration Map (Python -> C++)

| Current Python area | New C++ area | Notes |
|---|---|---|
| `app/main.py` | `native/bridge/bridge.cpp` + `native/server/server.cpp` | Bridge init + HTTP server startup |
| `app/core/middleware/*` | `native/server/middleware/*` | Request-id, firewall, decompression (each ≤400 LOC) |
| `app/modules/proxy/api.py` | `native/server/controllers/proxy_controller.cpp` | Keep endpoints and status behavior identical |
| `app/modules/proxy/service.py` (5210 LOC) | `native/proxy/proxy_service.cpp` + `native/proxy/stream/*.cpp` | **Must be split**: SSE, WS, compact, transcribe are separate files |
| `app/modules/proxy/load_balancer.py` | `native/balancer/scorer.cpp` + `native/balancer/strategies/*.cpp` | One file per strategy, shared scorer template |
| `app/core/balancer/logic.py` | `native/balancer/strategies/*.cpp` | Deterministic selection unit tests required |
| `app/core/openai/*` | `native/proxy/openai/*.cpp` | Payload normalization contract-critical |
| `app/modules/accounts/*` | `native/server/controllers/accounts_controller.cpp` + `native/db/repositories/account_repo.cpp` | Controller + repo, no god-service |
| `app/modules/oauth/*` | `native/auth/oauth/*.cpp` | `oauth_service.cpp` + `callback_server.cpp` + `token_refresh.cpp` |
| `app/modules/dashboard_auth/*` | `native/auth/dashboard/*.cpp` | `password_auth.cpp` + `totp_auth.cpp` + `session_manager.cpp` |
| `app/modules/api_keys/*` | `native/auth/api_keys/*.cpp` | `key_validator.cpp` + `limit_enforcer.cpp` + `reservation.cpp` |
| `app/modules/usage/*` | `native/usage/*.cpp` | `usage_fetcher.cpp` + `usage_updater.cpp` + `cost_calculator.cpp` + `trend_aggregator.cpp` |
| `app/modules/request_logs/*` | `native/db/repositories/request_log_repo.cpp` + `native/server/controllers/logs_controller.cpp` | Query logic in repo, HTTP in controller |
| `app/modules/settings/*` | `native/db/repositories/settings_repo.cpp` + `native/server/controllers/settings_controller.cpp` | Runtime settings table semantics |
| `app/modules/firewall/*` | `native/db/repositories/firewall_repo.cpp` + `native/server/middleware/firewall_filter.cpp` | Repo for CRUD, middleware for enforcement |
| `app/modules/sticky_sessions/*` | `native/proxy/session/*.cpp` | `sticky_resolver.cpp` + `session_cleanup.cpp` + repo |
| `app/db/*` | `native/db/connection/*.cpp` + `native/db/migration/*.cpp` | Pool + migration runner + integrity checks |

## 6.1 Load-Balancing Strategies and Math

The C++ balancer should support strategy selection via settings while sharing one common scoring model.
Legacy parity requirement: keep `usage_weighted` and `round_robin` available exactly as user-selectable modes.

Notation for account `a` and request `r`:

- `q(a)`: current in-flight requests
- `l(a)`: latency EWMA (ms)
- `e(a)`: error-rate EWMA in `[0,1]`
- `u_p(a)`: primary usage percent in `[0,1]`
- `u_s(a)`: secondary usage percent in `[0,1]` (fallback to `u_p(a)` if unavailable)
- `h(a)`: headroom score in `[0,1]`
- `m(a,r)`: capability flag (`1` if account can serve requested model/features, else `0`)
- `c(a)`: cooldown indicator (`1` if in cooldown/backoff, else `0`)
- `cost(a,r)`: expected cost per token/request for this model on this account

Normalization:

- `x_norm = (x - min(X)) / (max(X) - min(X) + eps)`
- `eps = 1e-9`

Headroom:

- `h(a) = 1 - (w_p * u_p(a) + w_s * u_s(a))`, where `w_p + w_s = 1`
- default: `w_p = 0.35`, `w_s = 0.65`

Composite score (lower is better):

`S(a,r) = alpha*q_norm(a) + beta*l_norm(a) + gamma*e(a) + delta*(1 - h(a)) + zeta*cost_norm(a,r) + eta*c(a)`

Eligibility:

- if `m(a,r) = 0`, set `S(a,r) = +inf` (hard reject)
- if status is paused/deactivated/rate-limited/quota-blocked, reject before scoring

Suggested default weights:

- `alpha=0.30`, `beta=0.25`, `gamma=0.20`, `delta=0.20`, `zeta=0.05`, `eta=1.00`

EWMA updates after each request:

- `l_t(a) = lambda_l * latency_ms + (1 - lambda_l) * l_{t-1}(a)` with `lambda_l = 0.2`
- `e_t(a) = lambda_e * is_error + (1 - lambda_e) * e_{t-1}(a)` with `lambda_e = 0.1`

Supported strategies:

1. `round_robin`
- `pick = accounts[i % N]`, then `i = i + 1`

2. `least_outstanding_requests`
- `pick = argmin_a q(a)` among eligible accounts

3. `latency_ewma`
- `pick = argmin_a l(a)` among eligible accounts

4. `success_rate_weighted`
- `p_success(a) = success(a) / (success(a) + error(a) + eps)`
- `w(a) = max(eps, p_success(a)^rho)` with `rho in [1,3]` (default `rho=2`)
- sample `a` with `P(a) = w(a) / sum_j w(j)`

5. `headroom_score`
- `pick = argmax_a h(a)` (equivalently `argmin (1-h(a))`)

6. `weighted_round_robin`
- static/dynamic weight `w(a) > 0`
- deficit form: `credit(a) += w(a)` each cycle, pick highest credit, then `credit(a) -= 1`

7. `cost_aware`
- `pick = argmin_a cost(a,r)` subject to headroom and error guardrails

8. `deadline_aware`
- if request deadline slack is small, bias toward low `l(a)` and low `e(a)`
- practical form: increase `beta` and `gamma` as slack decreases

9. `power_of_two_choices` (recommended default)
- sample two random eligible accounts `a,b`
- choose `pick = argmin(S(a,r), S(b,r))`
- gives near-best balancing quality with O(1) selection cost

Recommended production default:

- global strategy: `power_of_two_choices`
- per-request scoring: composite score `S(a,r)` above
- fallback: if only one eligible account, use it; if none, preserve current structured selection error behavior

## 7. Compatibility Requirements (Non-negotiable)

- Keep all existing public API routes and payload shapes
- Preserve OpenAI error envelope formats
- Preserve websocket accept/turn-state header semantics used by Codex-compatible clients
- Preserve DB table names and columns initially
- Read existing encryption key and encrypted token blobs without re-import
- Preserve request log status/error semantics powering dashboard filters

## 8. Database and Migration Strategy

## 8.1 Phase 1 DB Strategy

- Keep current schema as-is (same tables/columns/indexes)
- Implement C++ migration runner using SQL migration files and `schema_version` table
- Include a baseline migration representing latest schema equivalent to current head
- For existing installs:
  - Detect existing table set/version
  - Apply incremental SQL migrations only when needed

## 8.2 Startup Integrity

Replicate current startup behaviors:

- SQLite quick/full integrity checks
- Fail-fast option on migration errors
- Optional pre-migration backup for SQLite

## 9. Security and Auth Migration

## 9.1 Token Crypto

- Implement Fernet-compatible encrypt/decrypt to read existing `access_token_encrypted`, `refresh_token_encrypted`, `id_token_encrypted`
- Keep key-file semantics (`encryption.key`, mode `0600` where supported)
- Add tests with fixtures produced by current Python implementation

## 9.2 Dashboard Auth

- Preserve cookie name and TTL behavior
- Preserve password-first then optional TOTP flow
- Preserve rate-limit behavior on password/TOTP verification endpoints

## 9.3 API Key Auth and Limits

- Preserve key format semantics (`sk-clb-...`), hashing strategy, and validation errors
- Preserve reservation/settlement model for streamed vs compact requests

## 10. Electron Frontend Plan

## 10.1 Renderer

- Reuse current React codebase with minimal changes
- Replace `BrowserRouter` with `HashRouter` for packaged desktop reliability
- Keep existing feature modules and API contracts
- Renderer communicates with main process via IPC (not HTTP to self)

## 10.2 Main Process Responsibilities

- Load `tightrope-core.node` native module at startup
- Call `native.init(config)` to start the C++ backend (HTTP server + schedulers)
- Expose IPC handlers that proxy renderer requests to native module bridge
- Handle graceful shutdown via `native.shutdown()`
- No child process management needed — backend runs in-process

## 10.3 Native Module Bridge (app/src/main/native.ts)

Typed TypeScript wrapper around the N-API native module:

```typescript
interface NativeModule {
  init(config: NativeConfig): Promise<void>;
  shutdown(): Promise<void>;
  isRunning(): boolean;
  getHealth(): Promise<HealthStatus>;
  getAccounts(): Promise<Account[]>;
  getRequestLogs(query: LogQuery): Promise<RequestLog[]>;
  // ... all bridge methods typed here
}
```

## 10.4 Preload Boundary

- Minimal preload API only for desktop-only controls:
  - app version/build info
  - open log folder
  - restart backend (calls `native.shutdown()` then `native.init()`)
  - safe quit

## 10.5 Local Dev (No Docker)

- Build native module via `cmake-js` or `node-gyp`
- Run renderer via Vite + Electron
- Use local SQLite file in user data directory
- Hot-reload for renderer; rebuild native module only when C++ changes

## 11. No-Docker Packaging and Runtime

## 11.1 Distributions

- macOS: `.dmg` via `electron-builder`
- Windows: `.exe`/NSIS installer
- Linux: AppImage + `.deb`

Each package bundles:

- Electron app with native module (`tightrope-core.node`) embedded
- Migration SQL files
- No separate backend binary to manage — single process

## 11.2 Runtime Paths

Use OS-standard app data roots:

- macOS: `~/Library/Application Support/tightrope/`
- Windows: `%APPDATA%/tightrope/`
- Linux: `~/.local/share/tightrope/`

Store:

- `store.db`
- `encryption.key`
- logs
- settings overrides

## 12. Migration Phases and Gates

## Phase 0: Contract Freeze and Test Harness

Deliverables:

- Auto-captured contract fixtures from current Python service for critical endpoints
- Golden stream fixtures for SSE and websocket flows
- Baseline performance/profile snapshots

Exit gate:

- Fixture suite green against Python baseline

## Phase 1: C++ Skeleton + Infra

Deliverables:

- CMake project, CI build, lint/test scaffolding
- Config loader, logging, health endpoint, DB bootstrap
- Migration runner with SQLite baseline

Exit gate:

- Native app starts and passes startup/migration/integrity tests

## Phase 2: Admin APIs First

Deliverables:

- `accounts`, `settings`, `dashboard_auth`, `api_keys`, `firewall`, `sticky_sessions`, `request_logs`, `dashboard`, `usage`
- Electron renderer switched to C++ backend for these routes

Exit gate:

- Frontend parity on all dashboard workflows
- Existing frontend tests adapted and passing

## Phase 3: Proxy Core (SSE + compact + transcribe)

Deliverables:

- `/backend-api/codex/responses`, `/v1/responses`, `/responses/compact`, transcribe endpoints
- Payload normalization parity with contract tests

Exit gate:

- Existing API compatibility tests pass against C++ backend
- Streaming and compact parity verified by golden tests

## Phase 4: WebSocket Bridge + Sticky Affinity

Deliverables:

- WebSocket proxy endpoints and turn-state behavior
- Sticky session mapping and cleanup scheduler
- HTTP bridge session behavior where configured

Exit gate:

- Websocket integration tests pass
- Codex/OpenAI-client compatibility suite passes

## Phase 5: Hardening and Performance

Deliverables:

- Observability metrics and structured logs
- Error-path resilience and cancellation handling
- Query/index tuning for request logs and usage

Exit gate:

- Equal or better p95 latency on representative workloads
- Stable long-run soak tests

## Phase 6: Cutover

Deliverables:

- Desktop production builds for 3 OSes
- Migration/cutover docs
- Rollback plan

Exit gate:

- Pilot users validated on real workloads
- Rollback drill completed

## 13. Test Strategy (Rebuild)

- Unit tests per module (selection logic, payload normalization, API key limit math, auth)
- Contract tests replaying frozen request/response fixtures
- Streaming approval tests for SSE and websocket event order/content
- Integration tests on SQLite
- Electron E2E flow tests (login, account import, dashboard, settings)
- Backward-compat data tests:
  - load existing Python-created DB snapshots
  - decrypt existing token blobs using existing key files

## 14. Major Risks and Mitigations

1. Streaming/websocket behavior drift

- Mitigation: golden-wire fixtures and protocol-level assertions before cutover

2. Encryption compatibility break

- Mitigation: explicit Fernet compatibility tests with legacy fixtures

3. Query performance regressions in request logs

- Mitigation: preserve index strategy and benchmark search/filter paths early

4. OAuth flow platform edge cases in desktop

- Mitigation: keep both browser callback and manual callback paths; test on all OSes

5. Scope explosion from simultaneous frontend rewrite

- Mitigation: reuse existing React frontend inside Electron renderer; avoid UI rewrite during backend migration

## 15. Immediate Implementation Backlog (First 2 Weeks)

- [x] Create the top-level `native/` skeleton, CMake files, triplets, `vcpkg.json`, and build script.
- [x] Wire Electron main process, preload, and a typed native wrapper shell.
- [x] Add real `node-addon-api` bridge exports for init, shutdown, and health.
- [ ] Add CI build/test automation.
- [x] Implement config loader.
- [ ] Implement application-wide logging bootstrap.
- [x] Implement DB connection pool and migration runner with the real schema bootstrap.
- [x] Build the contract test harness that captures key endpoint baselines from `codex-lb` source contracts (no Python execution).
- [ ] Port `dashboard_auth` and `settings` first as the initial functional modules.

## 16. Bidirectional Database Synchronization

When two tightrope instances run on different machines, their local SQLite databases must stay in sync without a central server. The sync layer is built into the native module and operates transparently beneath the existing repository layer.

### 16.1 Architecture Overview

```
┌─────────────────────────┐           ┌─────────────────────────┐
│  Instance A (site_a)    │           │  Instance B (site_b)    │
│                         │           │                         │
│  ┌───────────────────┐  │           │  ┌───────────────────┐  │
│  │   Application     │  │           │  │   Application     │  │
│  │   Repositories    │  │           │  │   Repositories    │  │
│  └────────┬──────────┘  │           │  └────────┬──────────┘  │
│           │ writes      │           │           │ writes      │
│  ┌────────▼──────────┐  │           │  ┌────────▼──────────┐  │
│  │   SQLite + CDC    │  │  TLS/TCP  │  │   SQLite + CDC    │  │
│  │   Triggers        │──┼───────────┼──│   Triggers        │  │
│  └────────┬──────────┘  │  journal  │  └────────┬──────────┘  │
│  ┌────────▼──────────┐  │  exchange │  ┌────────▼──────────┐  │
│  │  _sync_journal    │  │           │  │  _sync_journal    │  │
│  │  _sync_peers      │  │           │  │  _sync_peers      │  │
│  │  _sync_tombstones │  │           │  │  _sync_tombstones │  │
│  └───────────────────┘  │           │  └───────────────────┘  │
└─────────────────────────┘           └─────────────────────────┘
```

**Core primitives:**

| Primitive | Purpose |
|-----------|---------|
| Hybrid Logical Clock (HLC) | Causally-ordered, monotonic timestamps combining wall clock + logical counter + site_id |
| Change journal | Append-only log of every row mutation with before/after snapshots |
| Deterministic conflict resolution | LWW by HLC with site_id tiebreaker; optional per-table merge functions |
| Sync protocol | Pull-based exchange of journal entries above peer's high-water mark |
| Undo log | Journal stores old values, enabling rollback of any sync batch |

### 16.2 Hybrid Logical Clock

Each instance maintains an HLC that advances on every local write and on receipt of remote changes:

```
HLC = { wall: uint64_t (ms), counter: uint32_t, site_id: uint32_t }
```

**Advancement rules:**
1. **Local event:** `wall = max(wall, now()); counter++`
2. **Receive remote HLC `r`:** `wall = max(wall, r.wall, now()); counter = (wall == r.wall) ? max(counter, r.counter) + 1 : 0`
3. **Comparison:** Compare `(wall, counter, site_id)` lexicographically — total order, no ties

This guarantees causal ordering without requiring synchronized clocks. NTP drift up to several seconds is tolerated because the logical counter breaks ties within the same wall-clock millisecond.

### 16.3 Schema Additions

```sql
-- Per-instance identity
CREATE TABLE _sync_meta (
  key       TEXT PRIMARY KEY,
  value     TEXT NOT NULL
);
-- Stores: site_id, hlc_wall, hlc_counter, schema_version

-- Change journal (append-only)
CREATE TABLE _sync_journal (
  seq           INTEGER PRIMARY KEY AUTOINCREMENT,
  hlc_wall      INTEGER NOT NULL,
  hlc_counter   INTEGER NOT NULL,
  site_id       INTEGER NOT NULL,
  table_name    TEXT    NOT NULL,
  row_pk        TEXT    NOT NULL,   -- JSON-encoded primary key
  op            TEXT    NOT NULL,   -- 'INSERT', 'UPDATE', 'DELETE'
  old_values    TEXT,               -- JSON of previous column values (NULL for INSERT)
  new_values    TEXT,               -- JSON of new column values (NULL for DELETE)
  checksum      TEXT    NOT NULL,   -- SHA-256 of (table_name, row_pk, op, old_values, new_values)
  applied       INTEGER DEFAULT 1, -- 1 = local origin, 2 = applied from remote
  batch_id      TEXT               -- groups entries applied together for atomic rollback
);

CREATE INDEX idx_journal_hlc ON _sync_journal(hlc_wall, hlc_counter, site_id);
CREATE INDEX idx_journal_table ON _sync_journal(table_name, row_pk);
CREATE INDEX idx_journal_batch ON _sync_journal(batch_id);

-- Peer tracking
CREATE TABLE _sync_peers (
  site_id         INTEGER PRIMARY KEY,
  address         TEXT,
  last_seen_seq   INTEGER DEFAULT 0,   -- highest seq we've sent to this peer
  last_recv_seq   INTEGER DEFAULT 0,   -- highest seq we've received from this peer
  last_sync_at    INTEGER,             -- epoch ms
  state           TEXT DEFAULT 'active' -- 'active', 'paused', 'unreachable'
);

-- Tombstones for soft deletes
CREATE TABLE _sync_tombstones (
  table_name    TEXT    NOT NULL,
  row_pk        TEXT    NOT NULL,
  deleted_at    INTEGER NOT NULL,   -- HLC wall
  site_id       INTEGER NOT NULL,
  PRIMARY KEY (table_name, row_pk)
);
```

**Shadow columns** added to every synced table:

```sql
ALTER TABLE <table> ADD COLUMN _hlc_wall    INTEGER DEFAULT 0;
ALTER TABLE <table> ADD COLUMN _hlc_counter INTEGER DEFAULT 0;
ALTER TABLE <table> ADD COLUMN _hlc_site    INTEGER DEFAULT 0;
```

### 16.4 Change Data Capture (CDC Triggers)

Auto-generated triggers on every synced table capture mutations into the journal:

```sql
-- Example for 'accounts' table
CREATE TRIGGER _cdc_accounts_insert AFTER INSERT ON accounts
BEGIN
  INSERT INTO _sync_journal (hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum)
  VALUES (
    _hlc_now_wall(), _hlc_now_counter(), _hlc_site_id(),
    'accounts', json_object('id', NEW.id), 'INSERT',
    NULL,
    json_object('email', NEW.email, 'provider', NEW.provider, ...),
    _checksum('accounts', json_object('id', NEW.id), 'INSERT', NULL, ...)
  );
  UPDATE accounts SET _hlc_wall = _hlc_now_wall(), _hlc_counter = _hlc_now_counter(), _hlc_site = _hlc_site_id()
    WHERE id = NEW.id;
END;

CREATE TRIGGER _cdc_accounts_update AFTER UPDATE ON accounts
WHEN OLD._hlc_wall != _hlc_now_wall() OR OLD._hlc_counter != _hlc_now_counter()
BEGIN
  INSERT INTO _sync_journal (...) VALUES (..., 'UPDATE', json_of(OLD.*), json_of(NEW.*), ...);
  UPDATE accounts SET _hlc_wall = _hlc_now_wall(), _hlc_counter = _hlc_now_counter(), _hlc_site = _hlc_site_id()
    WHERE id = NEW.id;
END;

CREATE TRIGGER _cdc_accounts_delete AFTER DELETE ON accounts
BEGIN
  INSERT INTO _sync_journal (...) VALUES (..., 'DELETE', json_of(OLD.*), NULL, ...);
  INSERT OR REPLACE INTO _sync_tombstones (table_name, row_pk, deleted_at, site_id)
    VALUES ('accounts', json_object('id', OLD.id), _hlc_now_wall(), _hlc_site_id());
END;
```

The `_hlc_now_*` and `_hlc_site_id` are C++ application-defined SQL functions registered via `sqlite3_create_function`. The HLC is advanced before each write transaction.

### 16.5 Sync Protocol

**Discovery:** Instances find each other automatically via mDNS/DNS-SD (Bonjour/Avahi). Each instance publishes a service record `_tightrope-sync._tcp` on the local network with TXT records containing `site_id`, `schema_version`, and `sync_port`. On startup and periodically, instances browse for this service type and automatically connect to discovered peers. Manual peer addresses are supported as fallback for cross-subnet deployments where mDNS doesn't reach.

```
Service type:  _tightrope-sync._tcp
Port:          9400 (configurable)
TXT records:   site_id=<hex>, schema=<version>, cluster=<cluster_name>
```

The `cluster` TXT field allows multiple independent tightrope deployments on the same network — instances only sync with peers advertising the same cluster name (default: `default`).

**Transport:** TCP with TLS 1.3, mutual certificate authentication. Configurable port (default 9400). Connection maintained as persistent bi-directional stream.

**Sync cycle (pull-based with push notification):**

```
Phase 1 — Handshake
  A → B: { site_id, schema_version, last_recv_seq_from_B }
  B → A: { site_id, schema_version, last_recv_seq_from_A }
  // Schema version mismatch → abort sync, log warning

Phase 2 — Journal exchange (bidirectional, interleaved)
  B → A: journal entries WHERE seq > A.last_recv_seq_from_B
         batched in chunks of 500, compressed with LZ4
  A → B: journal entries WHERE seq > B.last_recv_seq_from_A
         batched in chunks of 500, compressed with LZ4

Phase 3 — Application
  Each side applies received entries inside a single transaction:
    BEGIN IMMEDIATE;
    for each entry in batch:
      resolve_conflict(entry)  // may skip, apply, or merge
      apply_to_table(entry)
      mark entry in journal as applied=2
    UPDATE _sync_peers SET last_recv_seq = max_received_seq;
    COMMIT;
  // If ANY entry fails checksum → ROLLBACK entire batch, request re-send

Phase 4 — Acknowledgment
  A → B: { ack: true, applied_up_to_seq: N }
  B → A: { ack: true, applied_up_to_seq: M }
  // Each side updates last_seen_seq for the peer
```

**Push notification:** After a local write, the sync layer sends a lightweight "new data" ping to the peer over the persistent connection, triggering an immediate pull cycle rather than waiting for the interval timer.

### 16.6 Conflict Resolution

**Default: Last-Writer-Wins (LWW) by HLC**

When a remote entry targets a row that was also modified locally:

```cpp
ConflictResult resolve_lww(const JournalEntry& remote, const Row& local) {
    HLC remote_hlc = { remote.hlc_wall, remote.hlc_counter, remote.site_id };
    HLC local_hlc  = { local._hlc_wall, local._hlc_counter, local._hlc_site };

    if (remote_hlc > local_hlc) return APPLY_REMOTE;
    if (remote_hlc < local_hlc) return KEEP_LOCAL;
    // Equal HLC (impossible with unique site_id, but defensive)
    return remote.site_id > local._hlc_site ? APPLY_REMOTE : KEEP_LOCAL;
}
```

**Per-field merge (optional, per-table):**

For tables like `usage_history` where both instances may increment counters:

```cpp
ConflictResult resolve_field_merge(const JournalEntry& remote, const Row& local) {
    auto merged = json::object();
    for (auto& [key, remote_val] : remote.new_values.items()) {
        if (merge_strategy[table][key] == COUNTER) {
            // Both sides added delta from common ancestor
            auto ancestor_val = remote.old_values[key];
            auto local_delta  = local[key] - ancestor_val;
            auto remote_delta = remote_val - ancestor_val;
            merged[key] = ancestor_val + local_delta + remote_delta;
        } else {
            // Fall back to LWW for non-counter fields
            merged[key] = (remote_hlc > local_hlc) ? remote_val : local[key];
        }
    }
    return APPLY_MERGED(merged);
}
```

**Conflict log:** Every conflict resolution decision is logged to `_sync_journal` with both the winning and losing values preserved in `old_values`/`new_values`, enabling audit and manual override.

### 16.7 Journaling, Rollback, and Recovery

**Atomic batch rollback:**

Every sync application groups entries under a `batch_id`. To rollback:

```cpp
void rollback_batch(const std::string& batch_id) {
    auto entries = db.query(
        "SELECT * FROM _sync_journal WHERE batch_id = ? ORDER BY seq DESC",
        batch_id
    );

    db.exec("BEGIN IMMEDIATE");
    for (auto& entry : entries) {
        if (entry.op == "INSERT") {
            // Undo insert → delete the row
            db.exec("DELETE FROM " + entry.table_name + " WHERE " + pk_clause(entry.row_pk));
        } else if (entry.op == "UPDATE") {
            // Undo update → restore old values
            db.exec("UPDATE " + entry.table_name + " SET " + set_clause(entry.old_values)
                   + " WHERE " + pk_clause(entry.row_pk));
        } else if (entry.op == "DELETE") {
            // Undo delete → re-insert old values
            db.exec("INSERT INTO " + entry.table_name + columns_clause(entry.old_values)
                   + " VALUES " + values_clause(entry.old_values));
        }
    }
    db.exec("DELETE FROM _sync_journal WHERE batch_id = ?", batch_id);
    db.exec("COMMIT");
}
```

**Journal compaction:**

```cpp
void compact_journal(int retention_days) {
    auto cutoff = now_ms() - (retention_days * 86400000LL);
    // Only compact entries that both sides have acknowledged
    db.exec(R"(
        DELETE FROM _sync_journal
        WHERE hlc_wall < ?
          AND seq <= (SELECT MIN(last_seen_seq) FROM _sync_peers WHERE state = 'active')
    )", cutoff);

    // Compact tombstones older than 2x retention
    db.exec("DELETE FROM _sync_tombstones WHERE deleted_at < ?", cutoff / 2);
}
```

**Crash recovery:**

1. On startup, check for incomplete sync transactions (entries with `batch_id` but no corresponding ack in `_sync_peers`)
2. Roll back any incomplete batches — partial application is never visible
3. WAL mode ensures no corruption from mid-write crashes
4. The journal itself is the recovery log — no separate WAL needed for sync state

### 16.8 Integrity Guarantees

| Guarantee | Mechanism |
|-----------|-----------|
| No data loss | Journal stores full before/after snapshots; rollback restores exact prior state |
| No phantom writes | CDC triggers fire inside the same transaction as the application write |
| No partial sync | Entire batch applied in one transaction; ROLLBACK on any failure |
| No silent corruption | SHA-256 checksum per journal entry; mismatch → reject + re-request |
| Causal ordering | HLC guarantees happens-before relationship across sites |
| Convergence | Deterministic conflict resolution ensures both sites reach identical state |
| Tombstone consistency | Deletes propagate as tombstones; re-inserts after delete respected via HLC comparison |
| FK integrity | Apply order respects foreign key dependencies (parent before child inserts, child before parent deletes) |

### 16.9 Consensus Layer (Raft)

With 3+ peers, pairwise journal exchange is insufficient — peers can diverge during partitions and LWW may silently discard valid writes. The sync layer adds **Raft consensus** for tables that require linearizable writes, while keeping **CRDTs** for high-frequency commutative data that benefits from coordination-free merging.

#### 16.9.1 Why Hybrid Raft + CRDT

| Concern | Pure Raft | Pure CRDT | Hybrid |
|---------|-----------|-----------|--------|
| Config/account mutations | Linearizable | Eventually consistent (conflicts possible) | **Linearizable via Raft** |
| Usage counters | Unnecessary coordination overhead | Natural fit (commutative) | **CRDT G-Counter** |
| Sticky sessions | Blocks on leader unavailability | LWW works well | **CRDT LWW-Register** |
| Write availability during partition | Blocked (no quorum) | Always available | **Available for CRDT tables, blocked for Raft tables** |
| Convergence guarantee | Trivial (single leader) | Guaranteed by CRDT math | **Both** |

The hybrid model gives strong consistency where correctness matters (you don't want two nodes to simultaneously create conflicting API keys or delete each other's accounts) while preserving write availability for operational data.

#### 16.9.2 Raft Implementation

**State machine:**

```cpp
enum class RaftRole { Follower, Candidate, Leader };

struct RaftState {
    uint64_t current_term = 0;
    std::optional<uint32_t> voted_for;
    RaftRole role = RaftRole::Follower;
    uint32_t leader_id = 0;

    // Persistent log
    std::vector<LogEntry> log;
    uint64_t commit_index = 0;
    uint64_t last_applied = 0;

    // Leader-only volatile state
    std::unordered_map<uint32_t, uint64_t> next_index;   // per follower
    std::unordered_map<uint32_t, uint64_t> match_index;  // per follower
};

struct LogEntry {
    uint64_t term;
    uint64_t index;
    std::string table_name;
    std::string row_pk;       // JSON-encoded primary key
    std::string op;           // INSERT, UPDATE, DELETE
    std::string values;       // JSON payload
    std::string checksum;
};
```

**Election:**
- Heartbeat interval: 150ms. Election timeout: randomized 300–500ms.
- Candidate requests votes from all peers via `RequestVote` RPC.
- Majority required (⌊N/2⌋ + 1). With 3 nodes, tolerates 1 failure. With 5, tolerates 2.
- Pre-vote phase (Raft §9.6): candidates check if they *would* win before incrementing term, preventing disruption from partitioned nodes rejoining.

**Log replication:**
1. Client sends write to any node → forwarded to leader if not leader.
2. Leader appends entry to local log, sends `AppendEntries` RPC to all followers.
3. Once majority acknowledges → leader advances `commit_index`, applies to state machine (SQLite).
4. Followers apply committed entries to their own SQLite in order.
5. Exactly-once semantics via client request deduplication (client_id + sequence_number).

**Membership changes (joint consensus):**
- Adding/removing a peer uses Raft's joint consensus protocol (C_old,new → C_new).
- Prevents split-brain during membership transitions.
- Peers discovered via mDNS can be proposed for membership but must be approved through Raft.

**Persistent storage:**
- Raft log stored in a separate SQLite database (`raft.db`) to avoid contention with application data.
- `current_term` and `voted_for` fsynced before responding to RPCs.

#### 16.9.3 CRDT Types

For tables that don't need consensus, CRDTs provide conflict-free merging:

**G-Counter (grow-only counter):**
```cpp
// Each site maintains its own count. Merge = take max per site.
// Value = sum of all sites.
template<typename K = uint32_t>
struct GCounter {
    std::unordered_map<K, int64_t> counts;

    void increment(K site_id, int64_t delta) {
        counts[site_id] += delta;
    }

    int64_t value() const {
        int64_t sum = 0;
        for (auto& [_, v] : counts) sum += v;
        return sum;
    }

    void merge(const GCounter& other) {
        for (auto& [site, count] : other.counts) {
            counts[site] = std::max(counts[site], count);
        }
    }
};
```

**PN-Counter (positive-negative counter):**
```cpp
// Two G-Counters: one for increments, one for decrements.
// Value = P.value() - N.value(). Used for usage_history adjustments.
struct PNCounter {
    GCounter<> positive;
    GCounter<> negative;

    void add(uint32_t site_id, int64_t delta) {
        if (delta >= 0) positive.increment(site_id, delta);
        else negative.increment(site_id, -delta);
    }

    int64_t value() const { return positive.value() - negative.value(); }

    void merge(const PNCounter& other) {
        positive.merge(other.positive);
        negative.merge(other.negative);
    }
};
```

**LWW-Register (last-writer-wins register):**
```cpp
// Single value with HLC timestamp. Merge = keep highest HLC.
// Used for sticky_sessions: each session maps to an account.
template<typename T>
struct LWWRegister {
    T value;
    HLC timestamp;
    uint32_t site_id;

    void set(T new_value, HLC now, uint32_t site) {
        value = std::move(new_value);
        timestamp = now;
        site_id = site;
    }

    void merge(const LWWRegister& other) {
        if (other.timestamp > timestamp ||
            (other.timestamp == timestamp && other.site_id > site_id)) {
            value = other.value;
            timestamp = other.timestamp;
            site_id = other.site_id;
        }
    }
};
```

**OR-Set (observed-remove set):**
```cpp
// Add-wins semantics. Each element tagged with a unique (site_id, counter) pair.
// Remove only removes observed tags, concurrent adds survive.
// Used for ip_allowlist: concurrent add+remove of same IP doesn't lose the add.
struct ORSet {
    using Tag = std::pair<uint32_t, uint64_t>;  // (site_id, counter)
    std::unordered_map<std::string, std::set<Tag>> elements;
    std::unordered_map<uint32_t, uint64_t> counters;  // per-site monotonic

    Tag next_tag(uint32_t site_id) {
        return { site_id, ++counters[site_id] };
    }

    void add(const std::string& elem, uint32_t site_id) {
        elements[elem].insert(next_tag(site_id));
    }

    void remove(const std::string& elem) {
        elements.erase(elem);  // removes all observed tags
    }

    void merge(const ORSet& other) {
        for (auto& [elem, tags] : other.elements) {
            elements[elem].insert(tags.begin(), tags.end());
        }
    }

    std::vector<std::string> values() const {
        std::vector<std::string> result;
        for (auto& [elem, tags] : elements) {
            if (!tags.empty()) result.push_back(elem);
        }
        return result;
    }
};
```

#### 16.9.4 Table Consistency Model

| Table | Model | Rationale |
|-------|-------|-----------|
| accounts | **Raft** | Account add/remove/modify must be linearizable; concurrent conflicting edits could corrupt provider tokens |
| dashboard_settings | **Raft** | Config changes are infrequent; strong consistency prevents split-brain settings |
| api_keys | **Raft** | Key creation/revocation must be globally ordered; concurrent create+revoke must not resurrect a revoked key |
| api_key_limits | **Raft** | Limits are tightly coupled to api_keys; must be consistent |
| ip_allowlist | **CRDT OR-Set** | Add-wins semantics: if one node adds an IP while another removes it, the add wins — safer for security |
| usage_history | **CRDT PN-Counter** | High-frequency increments on every request; coordination-free merge by summing per-site deltas |
| sticky_sessions | **CRDT LWW-Register** | Session-to-account bindings can use LWW; concurrent rebinds take the latest |
| request_logs | **Local only** | Too high volume; each instance keeps its own logs |
| _sync_*, _raft_* | **Local only** | Per-instance infrastructure tables |

#### 16.9.5 Write Path by Model

**Raft-managed table write:**
```
App write(accounts, INSERT, {...})
  → SyncEngine.propose(entry)
    → if leader: append to Raft log, replicate to followers, await majority ack
    → if follower: forward to leader via RPC, leader proposes, follower awaits commit
  → on commit: apply to local SQLite inside transaction
  → return success to application
```

Latency: 1 RTT to majority (typically <5ms on LAN). If leader is local, 0 extra RTT.

**CRDT-managed table write:**
```
App write(usage_history, INCREMENT, {tokens: 1500})
  → apply to local SQLite immediately (no coordination)
  → CRDT state updated locally
  → on next sync cycle (or push notification): broadcast CRDT state to all peers
  → each peer merges CRDT state (idempotent, commutative, associative)
  → convergence guaranteed regardless of delivery order or duplication
```

Latency: 0 extra (local write). Convergence: within sync interval.

#### 16.9.6 Partition Handling

```
                    ┌─────────────────────────┐
                    │     Network Partition    │
                    └─────────────────────────┘
        ┌───────────────┐         ┌───────────────┐
        │  Partition A   │         │  Partition B   │
        │  (has majority)│         │  (minority)    │
        │                │         │                │
        │  Raft: ✓ reads │         │  Raft: ✗ reads │
        │        ✓ writes│         │        ✗ writes│
        │                │         │        (stale   │
        │  CRDT: ✓ reads │         │         reads   │
        │        ✓ writes│         │         ok)     │
        │                │         │                │
        │                │         │  CRDT: ✓ reads │
        │                │         │        ✓ writes│
        └───────────────┘         └───────────────┘

On heal: Raft leader catches up minority follower(s).
         CRDTs merge automatically — no conflicts possible.
```

- **Raft tables in minority partition:** Reads serve stale data (configurable: block reads or serve stale with warning). Writes are rejected with a clear error: "No quorum available — config changes require majority."
- **CRDT tables:** Always writable on both sides. Merge on reconnection is automatic and conflict-free.
- **Detection:** Leader steps down after election timeout with no follower responses. Followers detect leader absence and start election in the majority partition.

### 16.10 Table Sync Summary

| Table | Consistency | Replication |
|-------|-------------|-------------|
| accounts | Raft (linearizable) | Log replication |
| dashboard_settings | Raft | Log replication |
| api_keys | Raft | Log replication |
| api_key_limits | Raft | Log replication |
| ip_allowlist | CRDT OR-Set | State-based merge |
| usage_history | CRDT PN-Counter | State-based merge |
| sticky_sessions | CRDT LWW-Register | State-based merge |
| request_logs | Local only | Not replicated |

### 16.11 File Layout

```
native/
  sync/
    hlc.hpp                  # HLC struct, comparison, advancement
    hlc.cpp
    journal.hpp              # Journal read/write/compact
    journal.cpp
    cdc_triggers.hpp         # Trigger SQL generation per table
    cdc_triggers.cpp
    conflict_resolver.hpp    # LWW, field-merge, per-table strategies
    conflict_resolver.cpp
    sync_protocol.hpp        # Wire format, handshake, batch exchange
    sync_protocol.cpp
    sync_engine.hpp          # Orchestrator: routes writes to Raft or CRDT
    sync_engine.cpp
    consensus/
      raft_node.hpp          # Core Raft state machine
      raft_node.cpp
      raft_log.hpp           # Persistent log storage (raft.db)
      raft_log.cpp
      raft_rpc.hpp           # RequestVote, AppendEntries, InstallSnapshot
      raft_rpc.cpp
      membership.hpp         # Joint consensus membership changes
      membership.cpp
      leader_proxy.hpp       # Forward writes to current leader
      leader_proxy.cpp
    crdt/
      g_counter.hpp          # Grow-only counter
      pn_counter.hpp         # Positive-negative counter (uses two G-Counters)
      lww_register.hpp       # Last-writer-wins register with HLC
      or_set.hpp             # Observed-remove set (add-wins)
      crdt_store.hpp         # Serialization and SQLite persistence for CRDT state
      crdt_store.cpp
      crdt_sync.hpp          # State-based CRDT merge protocol
      crdt_sync.cpp
    transport/
      tls_stream.hpp         # TLS 1.3 TCP connection
      tls_stream.cpp
      lz4_codec.hpp          # Batch compression
      lz4_codec.cpp
      rpc_channel.hpp        # Multiplexed RPC over TLS (Raft + sync + CRDT)
      rpc_channel.cpp
    discovery/
      mdns_publisher.hpp     # Publish _tightrope-sync._tcp service
      mdns_publisher.cpp
      mdns_browser.hpp       # Browse and resolve peer services
      mdns_browser.cpp
      peer_manager.hpp       # Track discovered + manual peers, propose membership
      peer_manager.cpp
    checksum.hpp             # SHA-256 per-entry verification
    checksum.cpp
```

### 16.12 Bridge API Additions

```typescript
interface NativeModule {
  // ... existing methods ...

  // Cluster
  clusterEnable(config: ClusterConfig): Promise<void>;
  clusterDisable(): Promise<void>;
  clusterStatus(): Promise<ClusterStatus>;
  clusterAddPeer(address: string): Promise<void>;
  clusterRemovePeer(siteId: string): Promise<void>;

  // Sync (journal-level)
  syncTriggerNow(): Promise<void>;
  syncRollbackBatch(batchId: string): Promise<void>;
  syncGetJournal(options: { limit?: number, offset?: number }): Promise<JournalEntry[]>;
  syncGetConflicts(options: { since?: number }): Promise<ConflictRecord[]>;
}

interface ClusterConfig {
  cluster_name: string;
  sync_port: number;
  discovery_enabled: boolean;         // mDNS auto-discovery
  manual_peers: string[];             // fallback addresses
  sync_interval_seconds: number;      // for CRDT state exchange
  journal_retention_days: number;
  tls_enabled: boolean;
  tls_cert_path?: string;
  tls_key_path?: string;
}

interface ClusterStatus {
  enabled: boolean;
  site_id: string;
  cluster_name: string;
  role: 'leader' | 'follower' | 'candidate' | 'standalone';
  term: number;
  commit_index: number;
  leader_id: string | null;
  peers: PeerStatus[];
  journal_entries: number;
  pending_raft_entries: number;
  last_sync_at: number | null;
}

interface PeerStatus {
  site_id: string;
  address: string;
  state: 'connected' | 'disconnected' | 'unreachable';
  role: 'leader' | 'follower' | 'candidate';
  match_index: number;
  last_heartbeat_at: number | null;
  discovered_via: 'mdns' | 'manual';
}
```

## 17. Definition of Done

- All current public endpoints are served by C++ backend with parity-verified contracts.
- Electron app replaces browser-hosted frontend as primary UX.
- No Docker required for local dev, CI, or production use.
- Existing user data (`store.db`, `encryption.key`) works without manual migration.
- Existing core integration suite passes against the new stack.
