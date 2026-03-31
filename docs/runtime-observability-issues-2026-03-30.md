# Runtime Observability Gap Audit (2026-03-30)

## Scope
User-reported symptoms:
- No up/down traffic indicator activity per account.
- Requests not appearing in Logs UI.
- Uncertainty that requests are being persisted.

## Findings

### F1 — Logs controller is a stub (no runtime read API)
- File: `native/server/controllers/include/controllers/logs_controller.h`
- File: `native/server/controllers/src/logs_controller.cpp`
- Status: Empty placeholder only.
- Impact: No controller path to read `request_logs` from SQLite for renderer consumption.

### F2 — Admin runtime does not expose logs routes
- File: `native/server/src/admin_runtime.cpp`
- File: `native/server/include/internal/admin_runtime_parts.h`
- Status: Only settings/api-keys/accounts/oauth routes are wired.
- Impact: Even if persistence works, renderer has no `/api/logs` endpoint to query recent rows.

### F3 — Renderer logs view still uses seeded data
- File: `app/src/renderer/data/seed.ts`
- File: `app/src/renderer/state/useTightropeState.ts`
- File: `app/src/renderer/App.tsx`
- Status: `state.rows` initialized from `rowsSeed`; no runtime fetch/poll/stream path replaces it.
- Impact: Logs page can show static/demo entries instead of persisted runtime request logs.

### F4 — No IPC bridge for request-log retrieval
- File: `app/src/preload/index.ts`
- File: `app/src/main/ipc.ts`
- Status: Accounts/settings/firewall/etc are bridged; logs are not.
- Impact: In Electron dev mode (renderer on Vite host), relying on relative `/api/...` fetch is brittle; logs should use explicit IPC runtime bridge like other admin data.

### F5 — Request drawer contains synthetic fields
- File: `app/src/renderer/components/logs/RequestDrawer.tsx`
- Status: Retry/timing sections derive from hash/randomized placeholders, not runtime data.
- Impact: Log detail panel can present invented values that do not match actual runtime behavior.

### F6 — Traffic indicator path lacks resilience for cold-start races
- File: `app/src/renderer/state/useTightropeState.ts`
- Status:
  - Traffic WS frames are applied only if account list is already present.
  - No dedicated traffic snapshot fallback call if frames are missed.
- Impact: In startup timing races, indicators can stay at zero until another frame arrives.

## Non-finding clarification

### C1 — Request persistence path exists in runtime
- File: `native/server/src/server_routes.cpp`
- File: `native/server/src/proxy_request_logger.cpp`
- File: `native/db/repositories/src/request_log_repo.cpp`
- Status: Runtime persists request log rows for HTTP/SSE/WS code paths.
- Note: Visibility issue is primarily read-path/UI wiring, not only write-path absence.

## Required corrections
1. Implement `logs_controller` read API with pagination.
2. Wire `/api/logs` admin route.
3. Add IPC bridge methods for listing request logs.
4. Replace seeded logs table feed with runtime log fetch + periodic refresh.
5. Replace/remove synthetic request drawer sections to avoid fabricated telemetry.
6. Harden traffic indicator path with snapshot fallback and startup-race tolerance.

