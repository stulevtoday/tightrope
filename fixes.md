# Tightrope Guide Compliance Fixes

## High Priority

- [x] Replace custom consensus runtime with **NuRaft-backed implementation** (`native/sync/consensus/*`).
- [x] Add/adjust consensus tests to verify real NuRaft protocol behavior (leader election, log replication, commit progression).
- [x] Ensure raft/consensus test suite is green after NuRaft migration.

## Remaining Guide Deviations

- [x] Replace server runtime stub with real **uWebSockets** HTTP/WS server wiring.
- [x] Replace in-memory cluster placeholder logic in bridge with real consensus-backed cluster state.
- [x] Use **libuv** where event-loop integration is required by the guide (instead of unused linkage only).
- [x] Use **SQLiteCpp** RAII wrappers in DB layers where currently using raw sqlite3 directly.
- [x] Add **toml++**-based config file loading path (currently env-only loader).
- [x] Switch/centralize native logging through **quill** for backend runtime paths.
- [x] Wire vendored `vendor/mdns.h` into discovery implementation (replace current in-memory-only behavior).
- [x] Bring oversized source files back under guide limit (<= 400 LOC per `.cpp`/`.h`) for non-generated code.

## Proxy Parity Follow-Up (codex-lb)

- [x] Replace proxy default upstream transport stub with real upstream execution path.
- [x] Implement `POST /v1/chat/completions` parity route.
- [x] Implement `GET /api/codex/usage` parity route.
- [x] Wire API-key enforcement + request-id/decompression middleware in runtime path.
- [x] Implement HTTP responses session bridge continuity for `/v1/responses` and `/backend-api/codex/responses`.
- [x] Implement full turn-state continuity contract for websocket and HTTP responses.
- [x] Complete payload validation/sanitization parity (`include`, tools, truncation, message-role, input_file guards).
- [x] Implement streaming/compact retry-budget and stable proxy error-code parity.
- [x] Implement proxy request-log persistence parity.

## Proxy Parity Remaining (Toward 100%)

- [x] Advertise/honor downstream turn-state on HTTP/SSE `/v1/responses` and `/backend-api/codex/responses` runtime replies.
- [x] Advertise downstream turn-state on websocket upgrade handshake without breaking uWebSockets upgrade semantics.
- [x] Emit codex-lb-style rate-limit / credits headers on proxy route responses.
- [x] Align `/api/codex/usage` payload details to codex-lb rate window + credits snapshots (not just plan type baseline).
- [x] Add config-gated upstream payload trace logging parity in proxy runtime observability.
