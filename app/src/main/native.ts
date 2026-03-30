// Typed wrapper around the tightrope-core.node N-API native module

import fs from 'node:fs';
import path from 'node:path';

interface NativeConfig {
  host?: string;
  port?: number;
  oauth_callback_host?: string;
  oauth_callback_port?: number;
  db_path?: string;
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
  journal_entries: number;
  pending_raft_entries: number;
  last_sync_at: number | null;
}

interface ClusterConfig {
  cluster_name: string;
  site_id?: number;
  sync_port: number;
  discovery_enabled?: boolean;
  manual_peers?: string[];
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
}

function nativeModuleCandidates(): string[] {
  return Array.from(
    new Set([
      // cmake-js style output
      path.resolve(__dirname, '../../../build/Release/tightrope-core.node'),
      // Project-local fallbacks when app runs from ./app
      path.resolve(__dirname, '../../build/Release/tightrope-core.node'),
      // Fallbacks based on current working directory
      path.resolve(process.cwd(), '../build/Release/tightrope-core.node'),
      path.resolve(process.cwd(), 'build/Release/tightrope-core.node'),
    ])
  );
}

function loadNative(): NativeModule {
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

  if (loadError) {
    console.warn('[tightrope] native module load failed — running with stubs', loadError);
  } else {
    console.warn('[tightrope] native module not found — running with stubs');
  }

  return {
    async init() {},
    async shutdown() {},
    shutdownSync() {},
    isRunning: () => false,
    async getHealth() { return { status: 'error', uptime_ms: 0 }; },
    async clusterEnable() {},
    async clusterDisable() {},
    async clusterStatus() {
      return {
        enabled: false, site_id: '', cluster_name: '', role: 'standalone',
        term: 0, commit_index: 0, leader_id: null, peers: [],
        journal_entries: 0, pending_raft_entries: 0, last_sync_at: null,
      };
    },
    async clusterAddPeer() {},
    async clusterRemovePeer() {},
    async syncTriggerNow() {},
    async syncRollbackBatch() {},
  };
}

export const native: NativeModule = loadNative();
