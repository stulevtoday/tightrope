import type { ClusterStatus, SyncConflictResolution } from '../../../shared/types';

interface DatabaseSyncSectionProps {
  syncEnabled: boolean;
  syncSiteId: number;
  syncPort: number;
  syncDiscoveryEnabled: boolean;
  syncClusterName: string;
  manualPeerAddress: string;
  syncIntervalSeconds: number;
  syncConflictResolution: SyncConflictResolution;
  syncJournalRetentionDays: number;
  syncTlsEnabled: boolean;
  clusterStatus: ClusterStatus;
  onToggleSyncEnabled: () => void;
  onSetSyncSiteId: (siteId: number) => void;
  onSetSyncPort: (port: number) => void;
  onSetSyncDiscoveryEnabled: (enabled: boolean) => void;
  onSetSyncClusterName: (clusterName: string) => void;
  onSetManualPeerAddress: (value: string) => void;
  onAddManualPeer: () => void;
  onRemovePeer: (siteId: string) => void;
  onSetSyncIntervalSeconds: (seconds: number) => void;
  onSetSyncConflictResolution: (strategy: SyncConflictResolution) => void;
  onSetSyncJournalRetentionDays: (days: number) => void;
  onSetSyncTlsEnabled: (enabled: boolean) => void;
  onTriggerSyncNow: () => void;
}

function formatLastSync(lastSyncAt: number | null): string {
  if (lastSyncAt === null) return 'never';
  const deltaSeconds = Math.max(0, Math.floor((Date.now() - lastSyncAt) / 1000));
  if (deltaSeconds < 60) return `${deltaSeconds}s ago`;
  const minutes = Math.floor(deltaSeconds / 60);
  if (minutes < 60) return `${minutes}m ago`;
  const hours = Math.floor(minutes / 60);
  return `${hours}h ago`;
}

export function DatabaseSyncSection({
  syncEnabled,
  syncSiteId,
  syncPort,
  syncDiscoveryEnabled,
  syncClusterName,
  manualPeerAddress,
  syncIntervalSeconds,
  syncConflictResolution,
  syncJournalRetentionDays,
  syncTlsEnabled,
  clusterStatus,
  onToggleSyncEnabled,
  onSetSyncSiteId,
  onSetSyncPort,
  onSetSyncDiscoveryEnabled,
  onSetSyncClusterName,
  onSetManualPeerAddress,
  onAddManualPeer,
  onRemovePeer,
  onSetSyncIntervalSeconds,
  onSetSyncConflictResolution,
  onSetSyncJournalRetentionDays,
  onSetSyncTlsEnabled,
  onTriggerSyncNow,
}: DatabaseSyncSectionProps) {
  const peers = clusterStatus.peers ?? [];

  return (
    <div className="settings-group">
      <div className="settings-group-header">
        <h3>Database synchronization</h3>
        <p>Bidirectional sync between instances using journaled change replication with conflict resolution.</p>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Enable sync</strong>
          <span>Activate bidirectional replication with a remote instance</span>
        </div>
        <button
          className={`setting-toggle${syncEnabled ? ' on' : ''}`}
          type="button"
          aria-label="Toggle sync"
          onClick={onToggleSyncEnabled}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Instance ID</strong>
          <span>Unique site identifier for this node</span>
        </div>
        <input
          className="setting-input"
          type="number"
          min={1}
          value={syncSiteId}
          onChange={(event) => onSetSyncSiteId(Math.max(1, Number(event.target.value) || 1))}
          style={{ width: '90px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Sync port</strong>
          <span>TCP port for peer replication traffic</span>
        </div>
        <input
          className="setting-input"
          type="number"
          min={1}
          max={65535}
          value={syncPort}
          onChange={(event) => onSetSyncPort(Math.min(65535, Math.max(1, Number(event.target.value) || 1)))}
          style={{ width: '90px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Peer discovery</strong>
          <span>Find instances on the local network via mDNS (Bonjour/Avahi)</span>
        </div>
        <button
          className={`setting-toggle${syncDiscoveryEnabled ? ' on' : ''}`}
          type="button"
          aria-label="Toggle peer discovery"
          onClick={() => onSetSyncDiscoveryEnabled(!syncDiscoveryEnabled)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Cluster name</strong>
          <span>Only sync with peers advertising the same cluster</span>
        </div>
        <input
          className="setting-input"
          type="text"
          value={syncClusterName}
          onChange={(event) => onSetSyncClusterName(event.target.value)}
          style={{ width: '150px', textAlign: 'right' }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Discovered peers</strong>
          <span>Instances currently visible to this node</span>
        </div>
        <div style={{ display: 'grid', gap: '0.35rem', justifyItems: 'end' }}>
          {peers.length === 0 ? (
            <span style={{ color: 'var(--text-secondary)', fontSize: '11.5px' }}>none</span>
          ) : (
            peers.map((peer) => (
              <div key={`${peer.site_id}-${peer.address}`} style={{ display: 'flex', gap: '0.35rem', alignItems: 'center' }}>
                <span style={{ fontFamily: "'SF Mono',ui-monospace,monospace", color: 'var(--text-secondary)' }}>{peer.site_id}</span>
                <span style={{ color: 'var(--ok)' }}>{peer.address}</span>
                <button className="btn-danger" type="button" style={{ fontSize: '11px', padding: '0.15rem 0.4rem' }} onClick={() => onRemovePeer(peer.site_id)}>
                  Remove
                </button>
              </div>
            ))
          )}
        </div>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Manual peer</strong>
          <span>Fallback address for cross-subnet peers</span>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.35rem' }}>
          <input
            className="setting-input"
            type="text"
            placeholder="host:port"
            value={manualPeerAddress}
            onChange={(event) => onSetManualPeerAddress(event.target.value)}
            style={{ width: '160px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
          />
          <button className="btn-secondary" type="button" onClick={onAddManualPeer}>
            Add peer
          </button>
        </div>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Sync interval</strong>
          <span>Seconds between sync cycles (0 = manual trigger)</span>
        </div>
        <input
          className="setting-input"
          type="number"
          value={syncIntervalSeconds}
          min={0}
          max={86400}
          onChange={(event) => onSetSyncIntervalSeconds(Math.max(0, Number(event.target.value) || 0))}
          style={{ width: '70px', textAlign: 'right' }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Conflict resolution</strong>
          <span>Strategy when both instances modify the same row</span>
        </div>
        <select
          className="setting-select"
          style={{ minWidth: '180px' }}
          value={syncConflictResolution}
          onChange={(event) => onSetSyncConflictResolution(event.target.value as SyncConflictResolution)}
        >
          <option value="lww">Last-writer-wins (HLC)</option>
          <option value="site_priority">Site priority</option>
          <option value="field_merge">Per-field merge</option>
        </select>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Journal retention</strong>
          <span>Days to keep change journal entries before compaction</span>
        </div>
        <input
          className="setting-input"
          type="number"
          value={syncJournalRetentionDays}
          min={1}
          max={3650}
          onChange={(event) => onSetSyncJournalRetentionDays(Math.max(1, Number(event.target.value) || 1))}
          style={{ width: '70px', textAlign: 'right' }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Transport encryption</strong>
          <span>TLS for sync traffic between instances</span>
        </div>
        <button
          className={`setting-toggle${syncTlsEnabled ? ' on' : ''}`}
          type="button"
          aria-label="Toggle sync tls"
          onClick={() => onSetSyncTlsEnabled(!syncTlsEnabled)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Consistency model</strong>
        </div>
        <div style={{ fontSize: '11.5px', color: 'var(--text-secondary)', lineHeight: 1.5 }}>
          <div style={{ display: 'flex', gap: '0.4rem', alignItems: 'baseline' }}>
            <span style={{ color: 'var(--text)', fontWeight: 500 }}>Raft consensus</span>
            <span>accounts, settings, API keys</span>
          </div>
          <div style={{ display: 'flex', gap: '0.4rem', alignItems: 'baseline' }}>
            <span style={{ color: 'var(--text)', fontWeight: 500 }}>CRDT merge</span>
            <span>usage, sessions, IP allowlist</span>
          </div>
        </div>
      </div>
      <div className="setting-row" style={{ borderBottom: 'none' }}>
        <div className="setting-label">
          <strong>Cluster status</strong>
        </div>
        <div style={{ display: 'grid', gap: '0.3rem', fontSize: '11.5px', textAlign: 'right' }}>
          <div style={{ display: 'flex', gap: '0.8rem', alignItems: 'center', justifyContent: 'flex-end' }}>
            <span style={{ color: 'var(--accent)', fontWeight: 500 }}>
              {clusterStatus.enabled ? (clusterStatus.role === 'standalone' ? 'Standalone' : clusterStatus.role) : 'disabled'}
            </span>
            <span style={{ color: 'var(--text-secondary)' }}>Term {clusterStatus.term}</span>
            <span style={{ color: 'var(--text-secondary)' }}>Commit #{clusterStatus.commit_index}</span>
          </div>
          <div style={{ display: 'flex', gap: '0.8rem', alignItems: 'center', justifyContent: 'flex-end' }}>
            <span style={{ color: 'var(--ok)' }}>{clusterStatus.peers.length} peers connected</span>
            <span style={{ color: 'var(--text-secondary)' }}>Last sync: {formatLastSync(clusterStatus.last_sync_at)}</span>
            <span style={{ color: 'var(--text-secondary)' }}>Journal: {clusterStatus.journal_entries}</span>
          </div>
          <div style={{ display: 'flex', justifyContent: 'flex-end' }}>
            <button className="btn-secondary" type="button" onClick={onTriggerSyncNow}>
              Trigger sync now
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
