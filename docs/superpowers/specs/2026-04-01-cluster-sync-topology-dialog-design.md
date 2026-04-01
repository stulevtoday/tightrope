# Cluster Sync Topology Dialog — Design Spec

**Date:** 2026-04-01  
**Status:** Approved

---

## Overview

A real-time cluster synchronization topology dialog for the Tightrope GUI. It visualises the live Raft cluster state — leader, followers, replication lag, heartbeat health, probe latency, and ingress batch telemetry — with zero-latency updates driven by fine-grained native push events. It lives alongside the existing raw-stats view in `DatabaseSyncSection`, opened via a "View topology" button.

---

## Goals

- Display cluster topology (leader + followers) in a visual node-graph matching the `sketch/index.html` design
- Update immediately on any sync engine event — no polling in the renderer
- Modular: self-contained dialog component + dedicated state hook
- No changes to existing `DatabaseSyncSection` raw stats view beyond adding the "View topology" button

---

## Event Contract

Seven fine-grained event types flow through a single typed union `SyncEvent`. All events carry a `type` discriminant and a `ts` unix-ms timestamp stamped by the C++ emitter at enqueue time.

| `type` | Fires when | Payload fields |
|---|---|---|
| `journal_entry` | Journal entry written | `seq: number`, `table: string`, `op: string` |
| `peer_state_change` | Peer transitions state | `site_id: string`, `state: 'connected'\|'disconnected'\|'unreachable'`, `address: string` |
| `role_change` | This node changes Raft role | `role: 'leader'\|'follower'\|'candidate'`, `term: number`, `leader_id: string \| null` |
| `commit_advance` | `commit_index` advances | `commit_index: number`, `last_applied: number` |
| `term_change` | Raft term increments | `term: number` |
| `ingress_batch` | Batch accepted or rejected from a peer | `site_id: string`, `accepted: boolean`, `bytes: number`, `apply_duration_ms: number`, `replication_latency_ms: number` |
| `lag_alert` | Replication lag alert becomes active or clears | `active: boolean`, `lagging_peers: number`, `max_lag: number` |

---

## Architecture

```
C++ sync engine
  └── SyncEventEmitter (singleton, one TSFN)
        └── native-module.js (registerSyncEventCallback / unregisterSyncEventCallback)
              └── main/native.ts (NativeModule interface)
                    └── main/ipc.ts (webContents.send('sync:event', event))
                          └── preload/index.ts (onSyncEvent → ipcRenderer.on/removeListener)
                                └── renderer/state/useSyncTopology.ts (patch local ClusterStatus)
                                      └── renderer/components/dialogs/SyncTopologyDialog.tsx
```

---

## Layer Designs

### 1. C++ — `SyncEventEmitter`

**New files:**
- `native/sync/include/sync_event_emitter.h`
- `native/sync/src/sync_event_emitter.cpp`

A singleton class holding one `napi_threadsafe_function`. Any C++ thread calls `SyncEventEmitter::get().emit(event)` to enqueue a typed event. The TSFN serialises it to a JS object on the Node.js thread and invokes the registered JS callback.

**Emission call sites** (minimal additions to existing files):

| File | Event emitted |
|---|---|
| `src/journal.cpp` or `src/persistent_journal.cpp` | `journal_entry` |
| `src/consensus/raft_node.cpp` | `role_change`, `term_change`, `commit_advance` |
| `src/discovery/peer_manager.cpp` | `peer_state_change` |
| `src/transport/replication_ingress.cpp` | `ingress_batch` |
| `src/sync_engine.cpp` | `lag_alert` |

The TSFN is created when `registerSyncEventCallback` is called and released on `unregisterSyncEventCallback` or native shutdown. If no callback is registered, `emit()` is a no-op.

### 2. N-API Binding — `app/scripts/native-module.js`

Two new methods added to the native module binding:

- `registerSyncEventCallback(fn: (event: SyncEvent) => void): void` — stores the JS callback as a TSFN
- `unregisterSyncEventCallback(): void` — releases the TSFN

### 3. Main Process — `app/src/main/native.ts`

The `SyncEvent` discriminated union type is defined and exported here for the main-process side (7 variants matching the event contract above). A parallel definition lives in `renderer/shared/types.ts` for the renderer — the two execution contexts cannot share imports. `NativeModule` interface gains:

```ts
registerSyncEventCallback(fn: (event: SyncEvent) => void): void
unregisterSyncEventCallback(): void
```

Stub implementations are no-ops.

### 4. Main Process — `app/src/main/ipc.ts`

`registerIpcHandlers` receives the `BrowserWindow` reference (or `mainWindow` already available in the call site). After existing cluster/sync handlers:

```ts
native.registerSyncEventCallback((event) => {
  mainWindow.webContents.send('sync:event', event)
})
```

Registered once at startup. Always forwards — no subscribe/unsubscribe IPC needed. The renderer ignores events when the dialog is closed.

### 5. Preload — `app/src/preload/index.ts`

Added to `contextBridge.exposeInMainWorld('tightrope', { ... })`:

```ts
onSyncEvent: (listener: (event: SyncEvent) => void) => {
  const handler = (_event: unknown, payload: unknown) => {
    // type-guard payload, forward to listener
  }
  ipcRenderer.on('sync:event', handler)
  return () => ipcRenderer.removeListener('sync:event', handler)
}
```

Returns an unsubscribe function. Follows the exact same pattern as `onOauthDeepLink`.

### 6. Renderer Hook — `app/src/renderer/state/useSyncTopology.ts`

`useSyncTopology(open: boolean): { status: ClusterStatus | null, lastEventAt: number | null }`

Behaviour:
1. When `open` becomes `true`: calls `window.tightrope.getClusterStatus()` to seed initial state
2. Subscribes to `window.tightrope.onSyncEvent(handler)`, storing the returned unsubscribe fn
3. On each event, patches local `ClusterStatus` state surgically:
   - `peer_state_change` → find peer by `site_id`, update `state`
   - `commit_advance` → update `commit_index`, `last_applied`
   - `role_change` → update `role`, `term`, `leader_id`
   - `term_change` → update `term`
   - `journal_entry` → increment `journal_entries`
   - `ingress_batch` → find peer by `site_id`, update ingress telemetry fields
   - `lag_alert` → update `replication_lag_alert_active`, `replication_lagging_peers`, `replication_lag_max_entries`
4. Updates `lastEventAt` on every event
5. Cleanup: calls unsubscribe when `open` becomes `false` or component unmounts

Patch-on-event means no full re-fetch on every change — the dialog always reflects live native state.

### 7. Dialog Component — `app/src/renderer/components/dialogs/SyncTopologyDialog.tsx`

Props: `open: boolean`, `status: ClusterStatus | null`, `onClose: () => void`

Structure (matches `sketch/index.html` `#syncTopologyDialog`):

**Header**
- Eyebrow: "Cluster"
- Title: "Synchronization"
- Metadata row: `Leader <site_id>` (accent colour), `Term <n>`, `Commit #<n>`
- Close button

**Topology area**
- Two CSS layers over each other: `sync-topology-svg` (SVG) behind `sync-nodes-layer` (div)
- Leader card (top-centre): site_id, "Leader" role badge, stats — Commit, Applied, Term, Log
- Follower row (below): one card per peer in `clusterStatus.peers`
  - site_id, "Follower"/"Candidate" role badge
  - Stats: Match index, Lag (coloured `--warn` if > 0, `--ok` if 0), Heartbeat interval, Probe latency
- SVG `<line>` per peer, leader-card-centre → follower-card-centre, stroke colour:
  - `--accent` when replicating (lag > 0, state connected)
  - `--ok` when synced (lag === 0, state connected)
  - `--warn` when lagging significantly
  - `--text-secondary` when disconnected/unreachable

**Footer legend**
- Four swatches: Replicating down (`--accent`), Replicating up (`#8a9fd4`), Lagging (`--warn`), Synced (`--ok`)

**Integration in `DatabaseSyncSection`**
A "View topology" button added next to the existing "Trigger sync now" button. The open/close state and `useSyncTopology` hook live in `App.tsx` — the existing pattern mounts all dialogs there via `model.state.backendDialogOpen` / `model.openBackendDialog` etc. `SyncTopologyDialog` follows the same model.

---

## File Changelist

| File | Change |
|---|---|
| `native/sync/include/sync_event_emitter.h` | New |
| `native/sync/src/sync_event_emitter.cpp` | New |
| `native/sync/src/journal.cpp` or `persistent_journal.cpp` | Add `journal_entry` emit |
| `native/sync/src/consensus/raft_node.cpp` | Add `role_change`, `term_change`, `commit_advance` emits |
| `native/sync/src/discovery/peer_manager.cpp` | Add `peer_state_change` emit |
| `native/sync/src/transport/replication_ingress.cpp` | Add `ingress_batch` emit |
| `native/sync/src/sync_engine.cpp` | Add `lag_alert` emit |
| `app/scripts/native-module.js` | Add `registerSyncEventCallback`, `unregisterSyncEventCallback` |
| `app/src/main/native.ts` | Add `SyncEvent` type, two new `NativeModule` methods + stubs |
| `app/src/main/ipc.ts` | Register callback, forward via `webContents.send` |
| `app/src/preload/index.ts` | Expose `onSyncEvent` |
| `app/src/renderer/shared/types.ts` | Add `SyncEvent` discriminated union (renderer-side definition) |
| `app/src/renderer/state/useSyncTopology.ts` | New hook |
| `app/src/renderer/components/dialogs/SyncTopologyDialog.tsx` | New dialog component |
| `app/src/renderer/components/settings/sections/DatabaseSyncSection.tsx` | Add "View topology" button + prop |
| `app/src/renderer/App.tsx` | Mount dialog, add open state + hook to model |

---

## Out of Scope

- Replacing the raw stats dump in `DatabaseSyncSection`
- Polling fallback (push-only by design)
- CRDT-specific visualisation (journal / CRDT layer shown only via `journal_entry` count)
- Multi-window support (single main window assumed)
