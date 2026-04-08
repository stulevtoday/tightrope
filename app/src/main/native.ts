// Typed wrapper around the tightrope-core.node N-API native module

export type SyncEvent =
  | { type: 'journal_entry'; ts: number; seq: number; table: string; op: string }
  | { type: 'peer_state_change'; ts: number; site_id: string; state: 'connected' | 'disconnected' | 'unreachable'; address: string }
  | { type: 'role_change'; ts: number; role: 'leader' | 'follower' | 'candidate'; term: number; leader_id: string | null }
  | { type: 'commit_advance'; ts: number; commit_index: number; last_applied: number }
  | { type: 'term_change'; ts: number; term: number }
  | { type: 'ingress_batch'; ts: number; site_id: string; accepted: boolean; bytes: number; apply_duration_ms: number; replication_latency_ms: number }
  | { type: 'lag_alert'; ts: number; active: boolean; lagging_peers: number; max_lag: number }
  | { type: 'account_traffic'; ts: number; account_id: string; up_bytes: number; down_bytes: number; last_up_at_ms: number; last_down_at_ms: number }
  | { type: 'runtime_signal'; ts: number; level: 'info' | 'success' | 'warn' | 'error'; code: string; message: string; account_id: string | null };

import fs from 'node:fs';
import path from 'node:path';

interface NativeConfig {
  host?: string;
  port?: number;
  oauth_callback_host?: string;
  oauth_callback_port?: number;
  db_path?: string;
  db_passphrase?: string;
  config_path?: string;
}

interface HealthStatus {
  status: 'ok' | 'degraded' | 'error';
  uptime_ms: number;
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
  replication_lagging_peers: number;
  replication_lag_total_entries: number;
  replication_lag_max_entries: number;
  replication_lag_avg_entries: number;
  replication_lag_ewma_entries: number;
  replication_lag_ewma_samples: number;
  replication_lag_alert_threshold_entries: number;
  replication_lag_alert_sustained_refreshes: number;
  replication_lag_alert_streak: number;
  replication_lag_alert_active: boolean;
  replication_lag_last_alert_at: number | null;
  ingress_socket_accept_failures: number;
  ingress_socket_accepted_connections: number;
  ingress_socket_completed_connections: number;
  ingress_socket_failed_connections: number;
  ingress_socket_active_connections: number;
  ingress_socket_peak_active_connections: number;
  ingress_socket_tls_handshake_failures: number;
  ingress_socket_read_failures: number;
  ingress_socket_apply_failures: number;
  ingress_socket_handshake_ack_failures: number;
  ingress_socket_bytes_read: number;
  ingress_socket_total_connection_duration_ms: number;
  ingress_socket_last_connection_duration_ms: number;
  ingress_socket_max_connection_duration_ms: number;
  ingress_socket_connection_duration_ewma_ms: number;
  ingress_socket_connection_duration_le_10ms: number;
  ingress_socket_connection_duration_le_50ms: number;
  ingress_socket_connection_duration_le_250ms: number;
  ingress_socket_connection_duration_le_1000ms: number;
  ingress_socket_connection_duration_gt_1000ms: number;
  ingress_socket_max_buffered_bytes: number;
  ingress_socket_max_queued_frames: number;
  ingress_socket_max_queued_payload_bytes: number;
  ingress_socket_paused_read_cycles: number;
  ingress_socket_paused_read_sleep_ms: number;
  ingress_socket_last_connection_at: number | null;
  ingress_socket_last_failure_at: number | null;
  ingress_socket_last_failure_error: string | null;
  journal_entries: number;
  pending_raft_entries: number;
  last_sync_at: number | null;
}

interface ClusterConfig {
  cluster_name: string;
  site_id?: number;
  sync_port: number;
  discovery_enabled?: boolean;
  conflict_resolution?: 'lww' | 'site_priority' | 'field_merge';
  journal_retention_days?: number;
  manual_peers?: string[];
  require_handshake_auth?: boolean;
  cluster_shared_secret?: string;
  tls_enabled?: boolean;
  tls_verify_peer?: boolean;
  tls_ca_certificate_path?: string;
  tls_certificate_chain_path?: string;
  tls_private_key_path?: string;
  tls_pinned_peer_certificate_sha256?: string;
  sync_schema_version?: number;
  min_supported_sync_schema_version?: number;
  allow_schema_downgrade?: boolean;
  peer_probe_enabled?: boolean;
  peer_probe_interval_ms?: number;
  peer_probe_timeout_ms?: number;
  peer_probe_max_per_refresh?: number;
  peer_probe_fail_closed?: boolean;
  peer_probe_fail_closed_failures?: number;
  raft_election_timeout_lower_ms?: number;
  raft_election_timeout_upper_ms?: number;
  raft_heartbeat_interval_ms?: number;
  raft_rpc_failure_backoff_ms?: number;
  raft_max_append_size?: number;
  raft_thread_pool_size?: number;
  raft_test_mode?: boolean;
}

interface PeerStatus {
  site_id: string;
  address: string;
  state: 'connected' | 'disconnected' | 'unreachable';
  role: 'leader' | 'follower' | 'candidate';
  match_index: number;
  replication_lag_entries: number;
  consecutive_heartbeat_failures: number;
  consecutive_probe_failures: number;
  ingress_accepted_batches: number;
  ingress_rejected_batches: number;
  ingress_accepted_wire_bytes: number;
  ingress_rejected_wire_bytes: number;
  ingress_rejected_batch_too_large: number;
  ingress_rejected_backpressure: number;
  ingress_rejected_inflight_wire_budget: number;
  ingress_rejected_handshake_auth: number;
  ingress_rejected_handshake_schema: number;
  ingress_rejected_invalid_wire_batch: number;
  ingress_rejected_entry_limit: number;
  ingress_rejected_rate_limit: number;
  ingress_rejected_apply_batch: number;
  ingress_rejected_ingress_protocol: number;
  ingress_last_wire_batch_bytes: number;
  ingress_total_apply_duration_ms: number;
  ingress_last_apply_duration_ms: number;
  ingress_max_apply_duration_ms: number;
  ingress_apply_duration_ewma_ms: number;
  ingress_apply_duration_samples: number;
  ingress_total_replication_latency_ms: number;
  ingress_last_replication_latency_ms: number;
  ingress_max_replication_latency_ms: number;
  ingress_replication_latency_ewma_ms: number;
  ingress_replication_latency_samples: number;
  ingress_inflight_wire_batches: number;
  ingress_inflight_wire_batches_peak: number;
  ingress_inflight_wire_bytes: number;
  ingress_inflight_wire_bytes_peak: number;
  last_heartbeat_at: number | null;
  last_probe_at: number | null;
  last_probe_duration_ms: number | null;
  last_probe_error: string | null;
  last_ingress_rejection_at: number | null;
  last_ingress_rejection_reason: string | null;
  last_ingress_rejection_error: string | null;
  discovered_via: 'mdns' | 'manual';
}

interface NativeModule {
  init(config: NativeConfig): Promise<void>;
  shutdown(): Promise<void>;
  shutdownSync?: () => void;
  isRunning(): boolean;
  getHealth(): Promise<HealthStatus>;

  // Cluster
  clusterEnable(config: ClusterConfig): Promise<void>;
  clusterDisable(): Promise<void>;
  clusterStatus(): Promise<ClusterStatus>;
  clusterAddPeer(address: string): Promise<void>;
  clusterRemovePeer(siteId: string): Promise<void>;

  // Sync
  syncTriggerNow(): Promise<void>;
  syncRollbackBatch(batchId: string): Promise<void>;

  // Sync events
  registerSyncEventCallback(fn: (event: SyncEvent) => void): void;
  unregisterSyncEventCallback(): void;
}

function resolveNativeBuildMode(): 'debug' | 'release' {
  const rawMode = process.env.TIGHTROPE_NATIVE_BUILD_MODE;
  if (rawMode) {
    const mode = rawMode.trim().toLowerCase();
    if (mode === 'debug' || mode === 'release') {
      return mode;
    }
  }

  // In local dev, force debug-mode native loading.
  if (process.env.VITE_DEV_SERVER_URL) {
    return 'debug';
  }

  return 'release';
}

function packagedNativeModuleCandidates(): string[] {
  if (!process.resourcesPath) {
    return [];
  }

  return [
    path.resolve(process.resourcesPath, 'native', 'tightrope-core.node'),
    path.resolve(process.resourcesPath, 'app.asar.unpacked', 'native', 'tightrope-core.node'),
  ];
}

function nativeModuleCandidates(): string[] {
  const requestedMode = resolveNativeBuildMode();
  // On Windows the vcpkg graph (nuraft, SQLiteCpp, libuv, ...) does not produce a
  // debug variant that matches a Debug-config tightrope-core, so the builder aliases
  // debug -> release on win32. Mirror that here, otherwise the loader would look in
  // build-electron-debug/Debug and fail to find the module.
  const buildMode: 'debug' | 'release' =
    process.platform === 'win32' ? 'release' : requestedMode;
  // IMPORTANT: These paths must mirror app/scripts/native-module.js.
  // If debug/release paths diverge between builder and loader, Electron startup can crash.
  const buildDir = buildMode === 'debug' ? 'build-electron-debug' : 'build';
  const config = buildMode === 'debug' ? 'Debug' : 'Release';

  return Array.from(
    new Set([
      // Packaged app resources (electron-builder extraResources / asarUnpack)
      ...packagedNativeModuleCandidates(),
      // cmake-js style output
      path.resolve(__dirname, `../../../${buildDir}/${config}/tightrope-core.node`),
      path.resolve(__dirname, `../../../${buildDir}/tightrope-core.node`),
      // Project-local fallbacks when app runs from ./app
      path.resolve(__dirname, `../../${buildDir}/${config}/tightrope-core.node`),
      path.resolve(__dirname, `../../${buildDir}/tightrope-core.node`),
      // Fallbacks based on current working directory
      path.resolve(process.cwd(), `../${buildDir}/${config}/tightrope-core.node`),
      path.resolve(process.cwd(), `../${buildDir}/tightrope-core.node`),
      path.resolve(process.cwd(), `${buildDir}/${config}/tightrope-core.node`),
      path.resolve(process.cwd(), `${buildDir}/tightrope-core.node`),
    ])
  );
}

function createNativeStubs(): NativeModule {
  return {
    async init() {},
    async shutdown() {},
    shutdownSync() {},
    isRunning: () => false,
    async getHealth() {
      return { status: 'error', uptime_ms: 0 };
    },
    async clusterEnable() {},
    async clusterDisable() {},
    async clusterStatus() {
      return {
        enabled: false,
        site_id: '',
        cluster_name: '',
        role: 'standalone',
        term: 0,
        commit_index: 0,
        leader_id: null,
        peers: [],
        replication_lagging_peers: 0,
        replication_lag_total_entries: 0,
        replication_lag_max_entries: 0,
        replication_lag_avg_entries: 0,
        replication_lag_ewma_entries: 0,
        replication_lag_ewma_samples: 0,
        replication_lag_alert_threshold_entries: 0,
        replication_lag_alert_sustained_refreshes: 0,
        replication_lag_alert_streak: 0,
        replication_lag_alert_active: false,
        replication_lag_last_alert_at: null,
        ingress_socket_accept_failures: 0,
        ingress_socket_accepted_connections: 0,
        ingress_socket_completed_connections: 0,
        ingress_socket_failed_connections: 0,
        ingress_socket_active_connections: 0,
        ingress_socket_peak_active_connections: 0,
        ingress_socket_tls_handshake_failures: 0,
        ingress_socket_read_failures: 0,
        ingress_socket_apply_failures: 0,
        ingress_socket_handshake_ack_failures: 0,
        ingress_socket_bytes_read: 0,
        ingress_socket_total_connection_duration_ms: 0,
        ingress_socket_last_connection_duration_ms: 0,
        ingress_socket_max_connection_duration_ms: 0,
        ingress_socket_connection_duration_ewma_ms: 0,
        ingress_socket_connection_duration_le_10ms: 0,
        ingress_socket_connection_duration_le_50ms: 0,
        ingress_socket_connection_duration_le_250ms: 0,
        ingress_socket_connection_duration_le_1000ms: 0,
        ingress_socket_connection_duration_gt_1000ms: 0,
        ingress_socket_max_buffered_bytes: 0,
        ingress_socket_max_queued_frames: 0,
        ingress_socket_max_queued_payload_bytes: 0,
        ingress_socket_paused_read_cycles: 0,
        ingress_socket_paused_read_sleep_ms: 0,
        ingress_socket_last_connection_at: null,
        ingress_socket_last_failure_at: null,
        ingress_socket_last_failure_error: null,
        journal_entries: 0,
        pending_raft_entries: 0,
        last_sync_at: null,
      };
    },
    async clusterAddPeer() {},
    async clusterRemovePeer() {},
    async syncTriggerNow() {},
    async syncRollbackBatch() {},
    registerSyncEventCallback() {},
    unregisterSyncEventCallback() {},
  };
}

function loadNative(): NativeModule {
  if (process.env.TIGHTROPE_DISABLE_NATIVE === '1') {
    console.warn('[tightrope] native module disabled by TIGHTROPE_DISABLE_NATIVE=1; running with stubs');
    return createNativeStubs();
  }

  let loadError: unknown;
  for (const modulePath of nativeModuleCandidates()) {
    if (!fs.existsSync(modulePath)) {
      continue;
    }
    try {
      const loaded = require(modulePath);
      return loaded;
    } catch (error) {
      loadError = error;
    }
  }

  // In dev mode, falling back to stubs silently masks real failures (e.g. the
  // .node file is locked by a stale electron.exe on Windows). The UI looks like
  // it loads but every runtime request produces ECONNREFUSED because no real
  // HTTP server ever starts. Crash loudly instead so the developer sees it.
  const isDev = Boolean(process.env.VITE_DEV_SERVER_URL);
  if (loadError) {
    const message =
      '[tightrope] native module load failed — running with stubs. ' +
      'If this is Windows and the cause is "being used by another process", a stale ' +
      'electron.exe from a previous dev run is holding tightrope-core.node. ' +
      'Kill it (Task Manager, or `taskkill /F /IM electron.exe`) and retry.';
    if (isDev) {
      console.error(message, loadError);
      throw loadError;
    }
    console.warn(message, loadError);
  } else {
    const message = '[tightrope] native module not found — running with stubs';
    if (isDev) {
      console.error(message);
      throw new Error('native module not found');
    }
    console.warn(message);
  }

  return createNativeStubs();
}

export const native: NativeModule = loadNative();
