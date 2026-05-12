# Tightrope

Tightrope is an Electron desktop application backed by a native C++ runtime that provides account operations, routing control, request logging, and OpenAI compatible proxy endpoints on a local HTTP server.

## Contents

1. Project Scope
2. Architecture
3. Repository Layout
4. Build Toolchain and Dependencies
5. Compile and Package by Environment
6. Development and Test Workflows
7. Runtime API Surface
8. Configuration Model
9. Database Model
10. Build Artifacts
11. Troubleshooting

## 1. Project Scope

The project is designed for operators managing multiple upstream accounts from one local control plane. It combines a native runtime that serves admin and proxy routes with a desktop UI that drives that runtime through IPC and local HTTP calls.

Core capabilities include account lifecycle operations, routing policy control, sticky session continuity, request and event log inspection, firewall and API key controls, OAuth flows, and cluster sync management.

## 2. Architecture

Runtime flow:

`React renderer -> Electron preload bridge -> Electron main process -> tightrope-core.node -> local runtime server -> SQLite and upstream provider APIs`

Key runtime defaults:

- Runtime bind host: `127.0.0.1`
- Runtime bind port: `2455`
- OAuth callback listener: `localhost:1455`
- Default SQLite path: `store.db`

The native module is loaded from release or debug output roots depending on run mode. Release builds resolve from `build`. Electron debug mode resolves from `build-electron-debug`.

## 3. Repository Layout

| Path | Responsibility |
| --- | --- |
| `app` | Electron main process, preload bridge, React renderer, packaging scripts, UI tests |
| `native` | C++ runtime, server routes, auth, proxy, sync, SQLite repositories, native tests |
| `triplets` | vcpkg triplet definitions for macOS and Linux static builds |
| `vendor/opendht` | Vendored OpenDHT dependency integrated into native build graph |
| `build.sh` | macOS and Linux setup, build, test, and bundle entrypoint |
| `build.bat` | Windows setup, build, dev run, test, and packaging entrypoint |
| `vcpkg.json` | Native dependency manifest and pinned baseline |

## 4. Build Toolchain and Dependencies

### 4.1 Required Toolchain

| Component | Requirement |
| --- | --- |
| Node.js and npm | Required for Electron, TypeScript, Vite, test tooling, and cmake js orchestration |
| CMake | Version `3.21` or newer |
| C++ compiler | C++20 capable toolchain |
| Git | Required for vcpkg bootstrap and repository workflows |
| Visual Studio on Windows | Desktop development with C++ workload |

### 4.2 Desktop Stack

`electron`, `react`, `react-dom`, `typescript`, `vite`, `cmake-js`, `electron-builder`, `vitest`, `playwright`.

### 4.3 Native Stack

Managed through `vcpkg.json`, including NuRaft, uWebSockets, MbedTLS, Boost modules, SQLite3 with JSON1 and FTS5, SQLiteCpp, libsodium, OpenSSL, curl, libuv, lz4, glaze, quill, tomlplusplus, and Catch2.

## 5. Compile and Package by Environment

### 5.1 Build Matrix

| Host OS | Entrypoint | Native output roots | Package output |
| --- | --- | --- | --- |
| macOS | `./build.sh` | `build` and `build-debug` | `app/release` via `bundle-mac` |
| Linux | `./build.sh` | `build` and `build-debug` | no Linux packaging script in root helper |
| Windows | `build.bat` | built through `app` native build flow | `app/release` via `bundle` |

### 5.2 macOS

```bash
./build.sh setup
./build.sh native
./build.sh app
```

For native debug and native tests:

```bash
./build.sh debug
./build.sh test
```

For desktop bundle:

```bash
./build.sh bundle-mac
```

### 5.3 Linux

```bash
./build.sh setup
./build.sh native
./build.sh app
```

For native debug and native tests:

```bash
./build.sh debug
./build.sh test
```

### 5.4 Windows

```bat
build.bat setup
build.bat native
build.bat app
```

For desktop bundle:

```bat
build.bat bundle
```

## 6. Development and Test Workflows

### 6.1 Desktop Development

```bash
cd app
npm run dev
```

This runs TypeScript watch for Electron main, Vite for renderer, and Electron boot with debug native module checks.

### 6.2 App Test Workflow

```bash
cd app
npm test
```

This runs window shape checks, unit tests with Vitest, and UI tests with Playwright.

### 6.3 Native Test Workflow

```bash
./build.sh test
```

This builds `build-debug` with `BUILD_TESTING=ON` and executes `tightrope-tests`.

## 7. Runtime API Surface

### 7.1 Health and Runtime Control

| Route | Method | Purpose |
| --- | --- | --- |
| `/health` | `GET` | Runtime health and uptime |
| `/api/runtime/proxy` | `GET` | Proxy enabled state |
| `/api/runtime/proxy/start` | `POST` | Enable proxy routing |
| `/api/runtime/proxy/stop` | `POST` | Disable proxy routing |

### 7.2 Admin Routes

| Route family | Purpose |
| --- | --- |
| `/api/settings` | Dashboard and routing settings read and update |
| `/api/accounts` | Account list, traffic, import, pause, reactivate, pin, unpin, delete, usage refresh |
| `/api/accounts/import/sqlite/*` | SQLite preview and apply import flow |
| `/api/accounts/traffic/ws` | Realtime account traffic stream |
| `/api/logs` | Request and runtime log retrieval |
| `/api/sessions` | Sticky session inspection |
| `/api/firewall/ips` | Firewall allowlist CRUD |
| `/api/api-keys` | API key CRUD, regenerate, limit policy lifecycle |
| `/api/oauth/*` and `/auth/callback` | OAuth start, status, stop, restart, complete, manual callback |

### 7.3 Proxy and OpenAI Compatible Routes

| Route | Transport | Purpose |
| --- | --- | --- |
| `/api/models`, `/v1/models`, `/backend-api/codex/models` | HTTP | Model listing |
| `/v1/responses`, `/backend-api/codex/responses` | HTTP, SSE, WebSocket | Responses API proxy |
| `/v1/responses/compact`, `/backend-api/codex/responses/compact` | HTTP | Compact response path |
| `/v1/chat/completions` | HTTP and SSE | Chat completions proxy |
| `/v1/audio/transcriptions`, `/backend-api/transcribe` | HTTP | Transcription proxy |
| `/v1/memories/trace_summarize`, `/backend-api/codex/memories/trace_summarize` | HTTP | Memory summarize pass-through |
| `/api/codex/usage` | HTTP | Codex usage endpoint |

## 8. Configuration Model

### 8.1 Precedence

Configuration is resolved in this order:

1. Internal defaults in native config structures
2. TOML file loaded from `TIGHTROPE_CONFIG_PATH` or explicit bridge config path
3. Environment variables
4. Bridge init overrides passed from Electron main process

### 8.2 Runtime Config Fields

Supported base config fields include `host`, `port`, `db_path`, `config_path`, `log_level`, `sticky_ttl_ms`, and `sticky_cleanup_interval_ms`, plus nested TOML sections for server, database, logging, and proxy values.

Example `config.toml`:

```toml
host = "127.0.0.1"
port = 2455
db_path = "store.db"
log_level = "info"
sticky_ttl_ms = 1800000
sticky_cleanup_interval_ms = 60000

[database]
path = "store.db"

[logging]
level = "info"

[proxy]
sticky_ttl_ms = 1800000
sticky_cleanup_interval_ms = 60000
```

### 8.3 Core Environment Variables

| Variable | Default | Effect |
| --- | --- | --- |
| `TIGHTROPE_HOST` | `127.0.0.1` | Runtime host bind |
| `TIGHTROPE_PORT` | `2455` | Runtime port bind |
| `TIGHTROPE_DB_PATH` | `store.db` | SQLite database path |
| `TIGHTROPE_CONFIG_PATH` | unset | TOML config location |
| `TIGHTROPE_LOG_LEVEL` | `info` | Runtime logging level |
| `TIGHTROPE_STICKY_TTL_MS` | `1800000` | Sticky session ttl in ms |
| `TIGHTROPE_STICKY_CLEANUP_INTERVAL_MS` | `60000` | Sticky session cleanup cadence |

### 8.4 Upstream and OAuth Variables

| Variable | Default | Effect |
| --- | --- | --- |
| `TIGHTROPE_UPSTREAM_BASE_URL` | `https://chatgpt.com/backend-api` | Upstream proxy base URL |
| `TIGHTROPE_UPSTREAM_CONNECT_TIMEOUT_MS` | `10000` | Upstream connect timeout |
| `TIGHTROPE_UPSTREAM_TIMEOUT_MS` | `120000` | Upstream request timeout |
| `TIGHTROPE_UPSTREAM_BRIDGE_POLL_TIMEOUT_MS` | `1000` | WebSocket bridge read poll timeout |
| `TIGHTROPE_UPSTREAM_ENABLE_REQUEST_COMPRESSION` | `false` | Enables zstd request compression when available |
| `TIGHTROPE_CODEX_CLIENT_VERSION` | `1.0.0` | Version query for codex models endpoint |
| `TIGHTROPE_AUTH_BASE_URL` | `https://auth.openai.com` | OAuth auth host |
| `TIGHTROPE_OAUTH_CLIENT_ID` | project default | OAuth client id |
| `TIGHTROPE_OAUTH_REDIRECT_URI` | `http://localhost:1455/auth/callback` | OAuth callback URI |
| `TIGHTROPE_OAUTH_SCOPE` | `openid profile email` plus `offline_access` | OAuth scope list |
| `TIGHTROPE_OAUTH_TIMEOUT_SECONDS` | `30` | OAuth request timeout |

### 8.5 Token Encryption Variables

| Variable | Effect |
| --- | --- |
| `TIGHTROPE_TOKEN_ENCRYPTION_KEY_HEX` | Direct encryption key material |
| `TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE` | Encrypted key file path |
| `TIGHTROPE_TOKEN_ENCRYPTION_KEY_FILE_PASSPHRASE` | Passphrase for key file decrypt |
| `TIGHTROPE_TOKEN_ENCRYPTION_REQUIRE_ENCRYPTED_AT_REST` | Disallow plaintext token storage |
| `TIGHTROPE_TOKEN_ENCRYPTION_MIGRATE_PLAINTEXT_ON_READ` | Re-encrypt plaintext token rows on read |

### 8.6 Sync and Cluster Variables

Sync and cluster tuning supports a large variable surface for probe cadence, lag alerting, wire budgets, dead peer policies, TLS material, and journal compaction. Start with settings endpoints and sync UI fields, then apply environment overrides only for host specific tuning.

## 9. Database Model

### 9.1 Location and Lifecycle

SQLite is the primary store. The runtime opens the configured path in read write create mode with WAL enabled and a busy timeout. On first initialization, baseline schema creation is run from `native/migrations/001_baseline.sql`.

### 9.2 Core Tables

| Table | Purpose |
| --- | --- |
| `schema_version` | Migration version tracking |
| `accounts` | Account identities and token payload storage |
| `usage_history` | Token and cost usage snapshots |
| `additional_usage_history` | Additional quota keys and usage metrics |
| `request_logs` | Request level runtime log records |
| `sticky_sessions` | Session to account affinity for routing |
| `proxy_sticky_sessions` | Proxy continuity session mapping |
| `dashboard_settings` | Runtime and UI control state |
| `api_firewall_allowlist` | Firewall allowlist entries |
| `api_keys` | API key metadata and status |
| `api_key_limits` | Per key policy limits |
| `api_key_usage_reservations` | Reservation lifecycle for usage accounting |
| `api_key_usage_reservation_items` | Metric rows for reservations |

Sync metadata tables for replication journal and tombstones are also managed by sync schema logic.

### 9.3 SQLite Import Path

The admin API exposes preview and apply routes for importing accounts from external SQLite sources. The flow is explicit and staged to avoid blind overwrite behavior.

## 10. Build Artifacts

| Path | Produced by | Notes |
| --- | --- | --- |
| `build/tightrope-core.node` | `build.sh native` or release native build | Release native module candidate |
| `build-debug/tightrope-tests` | `build.sh debug` with testing enabled | Native test binary |
| `build-electron-debug` | `npm run ensure:native:debug` | Debug native module path for Electron dev |
| `app/dist` | `npm run build:main` and `npm run build:renderer` | Compiled Electron main and renderer output |
| `app/native/tightrope-core.node` | `npm run stage:native` | Staged native module for packaging |
| `app/release` | `npm run bundle:mac` or `npm run bundle:win` | Desktop package output |

## 11. Troubleshooting

### Windows MSVC tools not detected

If `build.bat setup` or `build.bat native` cannot find `vcvarsall.bat` or `cl.exe`, install or modify Visual Studio with the Desktop development with C++ workload.

```bat
winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive --norestart"
```

In Visual Studio Installer, choose Modify and enable Desktop development with C++. Make sure MSVC C++ build tools, the Windows 10/11 SDK, and C++ CMake tools for Windows are included. Then open a new terminal and rerun:

```bat
build.bat setup
build.bat native
```

To verify detection:

```bat
"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
```

### Native module not found

Run `./build.sh native` or `cd app && npm run ensure:native:release` and verify a `tightrope-core.node` exists in `build` or `build-electron-debug` depending on mode.

### Runtime does not bind on startup

Check for port conflicts on `2455` and verify no stale process is already bound. Confirm config or environment does not set an invalid host or port.

### SQLite errors on startup

Verify `TIGHTROPE_DB_PATH` points to a writable location and that the process has file permissions for database and WAL files.

### OAuth callback issues

Confirm callback host and port values align with configured redirect URI and that local firewall rules allow loopback callback traffic.
