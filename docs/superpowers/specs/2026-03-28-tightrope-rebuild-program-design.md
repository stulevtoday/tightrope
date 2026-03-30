# Tightrope C++ Rebuild Program Design

## Goal

Rebuild the current Python/FastAPI + React application into a C++ native backend plus Electron desktop shell while preserving public API compatibility, user-visible behavior, and existing SQLite/encryption data.

## Source Material

- Primary blueprint: `c++.md`
- Python reference implementation: `/Users/fabian/Development/codex-lb`
- Current C++ scaffold: `native/`, `app/`, `CMakeLists.txt`, `build.sh`

## Program Decomposition

The original blueprint is too broad for a single implementation plan. It contains six distinct workstreams with different risks, feedback loops, and test strategies.

### Track 1: Contract Baseline

Freeze the Python service behavior into reusable fixtures:

- HTTP request/response contracts
- SSE event sequences
- WebSocket transcripts
- DB snapshots and encryption compatibility fixtures
- Baseline performance numbers

This track makes the rebuild measurable instead of interpretive.

### Track 2: Foundation Runtime

Create the minimum working native runtime:

- fix CMake/vcpkg package resolution
- real N-API lifecycle bridge
- config loading and logger bootstrap
- health endpoint
- SQLite bootstrap, migration runner, integrity checks
- test harness that can actually execute

This is the first executable vertical slice and the only prerequisite for real C++ feature work.

### Track 3: Admin Control Plane

Port the dashboard and management APIs first:

- settings
- dashboard auth
- API keys and limits
- accounts
- firewall
- sticky session admin CRUD
- request logs
- usage/dashboard overview

This track has lower streaming complexity and gives fast parity feedback through deterministic CRUD and auth flows.

### Track 4: Proxy Compatibility

Port the compatibility-critical proxy path:

- payload normalization
- error envelope compatibility
- model registry
- balancer strategies
- JSON responses
- SSE streaming
- compact/transcribe endpoints
- WebSocket turn-state behavior
- sticky affinity runtime behavior

This track carries the highest protocol risk and must stay driven by frozen fixtures and golden stream tests.

### Track 5: Electron Desktop

Replace the placeholder shell with the real renderer:

- keep Electron main/preload thin
- load the native module in main
- migrate the React renderer from `/Users/fabian/Development/codex-lb/frontend`
- preserve API-client boundaries
- package the desktop app without Docker

This is intentionally downstream from admin/proxy parity so the UI migrates onto stable backend contracts.

### Track 6: Cluster Sync

Implement the optional multi-node sync architecture:

- HLC
- journal
- CDC triggers
- conflict resolution
- TLS transport
- CRDT state sync
- Raft-backed linearizable tables
- bridge APIs for cluster controls

This track is explicitly post-parity. It should stay behind a feature flag until single-node correctness is already proven.

## Dependency Order

Hard dependencies:

1. `foundation-runtime` must start by unblocking native configure/build.
2. `contract-baseline` fixture capture can start immediately, but C++ replay tests depend on `foundation-runtime`.
3. `admin-control-plane` depends on `foundation-runtime` and should consume `contract-baseline`.
4. `proxy-compatibility` depends on `foundation-runtime` and `contract-baseline`.
5. `electron-desktop` depends on `admin-control-plane` routes and the stable native bridge.
6. `cluster-sync` depends on the DB layer, auth/config/runtime primitives, and should not block single-node cutover.

Recommended execution sequence:

1. Unblock native configure/build.
2. Capture Python baseline fixtures.
3. Finish foundation runtime health/DB bootstrap.
4. Port admin control plane.
5. Port proxy core, then WebSocket/sticky behavior.
6. Migrate the renderer into Electron.
7. Harden and benchmark.
8. Implement cluster sync as a separate initiative.

## TDD Charter

Every feature track follows `@superpowers:test-driven-development`.

Rules:

- No production C++ code before a failing test exists.
- Fixture-driven compatibility work starts from a failing contract or golden-stream assertion.
- CRUD/API work starts from a failing repo/controller/integration test.
- Renderer work starts from failing component, hook, or IPC tests.
- Build/package/config changes use command-driven red/green:
  - reproduce the failing command
  - change the config
  - rerun the command until green

The only acceptable exception class is pure wiring where no meaningful automated test can exist before the config itself loads.

## File Ownership Strategy

To avoid recreating the Python monolith in C++:

- `bridge/` owns N-API only
- `server/controllers/` owns HTTP route mapping only
- `db/repositories/` owns SQL and result mapping only
- `auth/`, `proxy/`, `balancer/`, `usage/`, `sync/` own domain logic
- no controller should contain query construction
- no repository should contain protocol formatting
- no module should exceed the 400 LOC target

## Exit Gates

### Single-Node Parity Gate

The rebuild is not ready for desktop cutover until:

- admin workflows are green against parity tests
- proxy HTTP/SSE/WS compatibility suites are green
- existing `store.db` and `encryption.key` are readable
- p95 latency is at least not worse on representative workloads

### Cluster Gate

Cluster sync is not ready until:

- CRDT merge tests and Raft integration tests are green
- partition/rejoin simulations converge deterministically
- rollback and journal recovery scenarios are green
- cluster controls are hidden or disabled when the feature flag is off

## Generated Plans

- `docs/superpowers/plans/2026-03-28-contract-baseline.md`
- `docs/superpowers/plans/2026-03-28-foundation-runtime.md`
- `docs/superpowers/plans/2026-03-28-admin-control-plane.md`
- `docs/superpowers/plans/2026-03-28-proxy-compatibility.md`
- `docs/superpowers/plans/2026-03-28-electron-desktop.md`
- `docs/superpowers/plans/2026-03-28-cluster-sync.md`
