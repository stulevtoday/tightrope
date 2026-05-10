import type { ClusterStatus, SyncConflictResolution } from '../../../shared/types';
import { useTranslation } from 'react-i18next';

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
  syncRequireHandshakeAuth: boolean;
  syncClusterSharedSecret: string;
  syncTlsVerifyPeer: boolean;
  syncTlsCaCertificatePath: string;
  syncTlsCertificateChainPath: string;
  syncTlsPrivateKeyPath: string;
  syncTlsPinnedPeerCertificateSha256: string;
  syncSchemaVersion: number;
  syncMinSupportedSchemaVersion: number;
  syncAllowSchemaDowngrade: boolean;
  syncPeerProbeEnabled: boolean;
  syncPeerProbeIntervalMs: number;
  syncPeerProbeTimeoutMs: number;
  syncPeerProbeMaxPerRefresh: number;
  syncPeerProbeFailClosed: boolean;
  syncPeerProbeFailClosedFailures: number;
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
  onSetSyncRequireHandshakeAuth: (enabled: boolean) => void;
  onSetSyncClusterSharedSecret: (secret: string) => void;
  onSetSyncTlsVerifyPeer: (enabled: boolean) => void;
  onSetSyncTlsCaCertificatePath: (path: string) => void;
  onSetSyncTlsCertificateChainPath: (path: string) => void;
  onSetSyncTlsPrivateKeyPath: (path: string) => void;
  onSetSyncTlsPinnedPeerCertificateSha256: (value: string) => void;
  onSetSyncSchemaVersion: (version: number) => void;
  onSetSyncMinSupportedSchemaVersion: (version: number) => void;
  onSetSyncAllowSchemaDowngrade: (enabled: boolean) => void;
  onSetSyncPeerProbeEnabled: (enabled: boolean) => void;
  onSetSyncPeerProbeIntervalMs: (value: number) => void;
  onSetSyncPeerProbeTimeoutMs: (value: number) => void;
  onSetSyncPeerProbeMaxPerRefresh: (value: number) => void;
  onSetSyncPeerProbeFailClosed: (enabled: boolean) => void;
  onSetSyncPeerProbeFailClosedFailures: (value: number) => void;
  onTriggerSyncNow: () => void;
  onOpenSyncTopology: () => void;
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
  syncRequireHandshakeAuth,
  syncClusterSharedSecret,
  syncTlsVerifyPeer,
  syncTlsCaCertificatePath,
  syncTlsCertificateChainPath,
  syncTlsPrivateKeyPath,
  syncTlsPinnedPeerCertificateSha256,
  syncSchemaVersion,
  syncMinSupportedSchemaVersion,
  syncAllowSchemaDowngrade,
  syncPeerProbeEnabled,
  syncPeerProbeIntervalMs,
  syncPeerProbeTimeoutMs,
  syncPeerProbeMaxPerRefresh,
  syncPeerProbeFailClosed,
  syncPeerProbeFailClosedFailures,
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
  onSetSyncRequireHandshakeAuth,
  onSetSyncClusterSharedSecret,
  onSetSyncTlsVerifyPeer,
  onSetSyncTlsCaCertificatePath,
  onSetSyncTlsCertificateChainPath,
  onSetSyncTlsPrivateKeyPath,
  onSetSyncTlsPinnedPeerCertificateSha256,
  onSetSyncSchemaVersion,
  onSetSyncMinSupportedSchemaVersion,
  onSetSyncAllowSchemaDowngrade,
  onSetSyncPeerProbeEnabled,
  onSetSyncPeerProbeIntervalMs,
  onSetSyncPeerProbeTimeoutMs,
  onSetSyncPeerProbeMaxPerRefresh,
  onSetSyncPeerProbeFailClosed,
  onSetSyncPeerProbeFailClosedFailures,
  onTriggerSyncNow,
  onOpenSyncTopology,
}: DatabaseSyncSectionProps) {
  const { t } = useTranslation();
  const peers = clusterStatus.peers ?? [];
  const connectedPeers = peers.filter((peer) => peer.state === 'connected').length;
  const unreachablePeers = peers.filter((peer) => peer.state === 'unreachable').length;

  return (
    <div className="settings-group">
      <div className="settings-group-header">
        <h3>{t('settings.db_sync_title')}</h3>
        <p>{t('settings.db_sync_desc')}</p>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_enable')}</strong>
          <span>{t('settings.db_sync_enable_desc')}</span>
        </div>
        <button
          className={`setting-toggle${syncEnabled ? ' on' : ''}`}
          type="button"
          aria-label={t('settings.db_sync_toggle_aria')}
          onClick={onToggleSyncEnabled}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_instance_id')}</strong>
          <span>{t('settings.db_sync_instance_id_desc')}</span>
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
          <strong>{t('settings.db_sync_port')}</strong>
          <span>{t('settings.db_sync_port_desc')}</span>
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
          <strong>{t('settings.db_sync_peer_discovery')}</strong>
          <span>{t('settings.db_sync_peer_discovery_desc')}</span>
        </div>
        <button
          className={`setting-toggle${syncDiscoveryEnabled ? ' on' : ''}`}
          type="button"
          aria-label={t('settings.db_sync_peer_discovery_aria')}
          onClick={() => onSetSyncDiscoveryEnabled(!syncDiscoveryEnabled)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_cluster_name')}</strong>
          <span>{t('settings.db_sync_cluster_name_desc')}</span>
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
          <strong>{t('settings.db_sync_discovered_peers')}</strong>
          <span>{t('settings.db_sync_discovered_peers_desc')}</span>
        </div>
        <div style={{ display: 'grid', gap: '0.35rem', justifyItems: 'end' }}>
          {peers.length === 0 ? (
            <span style={{ color: 'var(--text-secondary)', fontSize: '11.5px' }}>{t('settings.db_sync_none')}</span>
          ) : (
            peers.map((peer) => (
              <div key={`${peer.site_id}-${peer.address}`} style={{ display: 'flex', gap: '0.35rem', alignItems: 'center' }}>
                <span style={{ fontFamily: "'SF Mono',ui-monospace,monospace", color: 'var(--text-secondary)' }}>{peer.site_id}</span>
                <span
                  style={{
                    color: peer.state === 'connected' ? 'var(--ok)' : peer.state === 'unreachable' ? 'var(--danger)' : 'var(--warn)',
                  }}
                >
                  {peer.address}
                </span>
                <span style={{ color: 'var(--text-secondary)', fontSize: '11px' }}>
                  {peer.state} · lag {peer.replication_lag_entries} · hb failures {peer.consecutive_heartbeat_failures} · probe failures{' '}
                  {peer.consecutive_probe_failures} · source {peer.discovered_via} · last probe {formatLastSync(peer.last_probe_at)}
                  {peer.last_probe_duration_ms !== null ? ` · probe ${peer.last_probe_duration_ms}ms` : ''}
                  {peer.last_probe_error ? ` · probe error: ${peer.last_probe_error}` : ''}
                </span>
                <button className="btn-danger" type="button" style={{ fontSize: '11px', padding: '0.15rem 0.4rem' }} onClick={() => onRemovePeer(peer.site_id)}>
                  {t('settings.db_sync_remove_peer')}
                </button>
              </div>
            ))
          )}
        </div>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_manual_peer')}</strong>
          <span>{t('settings.db_sync_manual_peer_desc')}</span>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.35rem' }}>
          <input
            className="setting-input"
            type="text"
            placeholder={t('settings.db_sync_manual_peer_placeholder')}
            value={manualPeerAddress}
            onChange={(event) => onSetManualPeerAddress(event.target.value)}
            style={{ width: '160px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
          />
          <button className="btn-secondary" type="button" onClick={onAddManualPeer}>
            {t('settings.db_sync_add_peer')}
          </button>
        </div>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_interval')}</strong>
          <span>{t('settings.db_sync_interval_desc')}</span>
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
          <strong>{t('settings.db_sync_conflict_resolution')}</strong>
          <span>{t('settings.db_sync_conflict_resolution_desc')}</span>
        </div>
        <select
          className="setting-select"
          style={{ minWidth: '180px' }}
          value={syncConflictResolution}
          onChange={(event) => onSetSyncConflictResolution(event.target.value as SyncConflictResolution)}
        >
          <option value="lww">{t('settings.db_sync_conflict_lww_hlc')}</option>
          <option value="site_priority">{t('settings.db_sync_conflict_site_priority')}</option>
          <option value="field_merge">{t('settings.db_sync_conflict_per_field')}</option>
        </select>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_journal_retention')}</strong>
          <span>{t('settings.db_sync_journal_retention_desc')}</span>
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
          <strong>{t('settings.db_sync_transport_encryption')}</strong>
          <span>{t('settings.db_sync_transport_encryption_desc')}</span>
        </div>
        <button
          className={`setting-toggle${syncTlsEnabled ? ' on' : ''}`}
          type="button"
          aria-label={t('settings.db_sync_tls_toggle_aria')}
          onClick={() => onSetSyncTlsEnabled(!syncTlsEnabled)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_handshake_auth')}</strong>
          <span>{t('settings.db_sync_handshake_auth_desc')}</span>
        </div>
        <button
          className={`setting-toggle${syncRequireHandshakeAuth ? ' on' : ''}`}
          type="button"
          aria-label={t('settings.db_sync_handshake_auth_aria')}
          onClick={() => onSetSyncRequireHandshakeAuth(!syncRequireHandshakeAuth)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_cluster_secret')}</strong>
          <span>{t('settings.db_sync_cluster_secret_desc')}</span>
        </div>
        <input
          className="setting-input"
          type="password"
          placeholder={t('settings.db_sync_cluster_secret_placeholder')}
          value={syncClusterSharedSecret}
          onChange={(event) => onSetSyncClusterSharedSecret(event.target.value)}
          style={{ width: '220px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_tls_verify_peer')}</strong>
          <span>{t('settings.db_sync_tls_verify_peer_desc')}</span>
        </div>
        <button
          className={`setting-toggle${syncTlsVerifyPeer ? ' on' : ''}`}
          type="button"
          aria-label={t('settings.db_sync_tls_verify_peer_aria')}
          onClick={() => onSetSyncTlsVerifyPeer(!syncTlsVerifyPeer)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_tls_ca_path')}</strong>
          <span>{t('settings.db_sync_tls_ca_path_desc')}</span>
        </div>
        <input
          className="setting-input"
          type="text"
          placeholder={t('settings.db_sync_tls_ca_path_placeholder')}
          value={syncTlsCaCertificatePath}
          onChange={(event) => onSetSyncTlsCaCertificatePath(event.target.value)}
          style={{ width: '260px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_tls_cert_chain_path')}</strong>
          <span>{t('settings.db_sync_tls_cert_chain_path_desc')}</span>
        </div>
        <input
          className="setting-input"
          type="text"
          placeholder={t('settings.db_sync_tls_cert_chain_path_placeholder')}
          value={syncTlsCertificateChainPath}
          onChange={(event) => onSetSyncTlsCertificateChainPath(event.target.value)}
          style={{ width: '260px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_tls_private_key_path')}</strong>
          <span>{t('settings.db_sync_tls_private_key_path_desc')}</span>
        </div>
        <input
          className="setting-input"
          type="text"
          placeholder={t('settings.db_sync_tls_private_key_path_placeholder')}
          value={syncTlsPrivateKeyPath}
          onChange={(event) => onSetSyncTlsPrivateKeyPath(event.target.value)}
          style={{ width: '260px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_pinned_sha256')}</strong>
          <span>{t('settings.db_sync_pinned_sha256_desc')}</span>
        </div>
        <input
          className="setting-input"
          type="text"
          placeholder={t('settings.db_sync_pinned_sha256_placeholder')}
          value={syncTlsPinnedPeerCertificateSha256}
          onChange={(event) => onSetSyncTlsPinnedPeerCertificateSha256(event.target.value)}
          style={{ width: '260px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_schema_version')}</strong>
          <span>{t('settings.db_sync_schema_version_desc')}</span>
        </div>
        <input
          className="setting-input"
          type="number"
          min={1}
          max={1000000}
          value={syncSchemaVersion}
          onChange={(event) => onSetSyncSchemaVersion(Math.max(1, Number(event.target.value) || 1))}
          style={{ width: '90px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_min_supported_schema')}</strong>
          <span>{t('settings.db_sync_min_supported_schema_desc')}</span>
        </div>
        <input
          className="setting-input"
          type="number"
          min={1}
          max={1000000}
          value={syncMinSupportedSchemaVersion}
          onChange={(event) => onSetSyncMinSupportedSchemaVersion(Math.max(1, Number(event.target.value) || 1))}
          style={{ width: '90px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_allow_schema_downgrade')}</strong>
          <span>{t('settings.db_sync_allow_schema_downgrade_desc')}</span>
        </div>
        <button
          className={`setting-toggle${syncAllowSchemaDowngrade ? ' on' : ''}`}
          type="button"
          aria-label={t('settings.db_sync_allow_schema_downgrade_aria')}
          onClick={() => onSetSyncAllowSchemaDowngrade(!syncAllowSchemaDowngrade)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_peer_probe_enabled')}</strong>
          <span>{t('settings.db_sync_peer_probe_enabled_desc')}</span>
        </div>
        <button
          className={`setting-toggle${syncPeerProbeEnabled ? ' on' : ''}`}
          type="button"
          aria-label={t('settings.db_sync_peer_probe_enabled_aria')}
          onClick={() => onSetSyncPeerProbeEnabled(!syncPeerProbeEnabled)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_peer_probe_interval')}</strong>
          <span>{t('settings.db_sync_peer_probe_interval_desc')}</span>
        </div>
        <input
          className="setting-input"
          type="number"
          min={100}
          max={300000}
          value={syncPeerProbeIntervalMs}
          onChange={(event) => onSetSyncPeerProbeIntervalMs(Number(event.target.value) || 0)}
          style={{ width: '110px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_peer_probe_timeout')}</strong>
          <span>{t('settings.db_sync_peer_probe_timeout_desc')}</span>
        </div>
        <input
          className="setting-input"
          type="number"
          min={50}
          max={60000}
          value={syncPeerProbeTimeoutMs}
          onChange={(event) => onSetSyncPeerProbeTimeoutMs(Number(event.target.value) || 0)}
          style={{ width: '110px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_max_probes')}</strong>
          <span>{t('settings.db_sync_max_probes_desc')}</span>
        </div>
        <input
          className="setting-input"
          type="number"
          min={1}
          max={64}
          value={syncPeerProbeMaxPerRefresh}
          onChange={(event) => onSetSyncPeerProbeMaxPerRefresh(Number(event.target.value) || 0)}
          style={{ width: '90px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_fail_closed')}</strong>
          <span>{t('settings.db_sync_fail_closed_desc')}</span>
        </div>
        <button
          className={`setting-toggle${syncPeerProbeFailClosed ? ' on' : ''}`}
          type="button"
          aria-label={t('settings.db_sync_fail_closed_aria')}
          onClick={() => onSetSyncPeerProbeFailClosed(!syncPeerProbeFailClosed)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_fail_closed_threshold')}</strong>
          <span>{t('settings.db_sync_fail_closed_threshold_desc')}</span>
        </div>
        <input
          className="setting-input"
          type="number"
          min={1}
          max={1000}
          value={syncPeerProbeFailClosedFailures}
          onChange={(event) => onSetSyncPeerProbeFailClosedFailures(Number(event.target.value) || 0)}
          style={{ width: '90px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_sync_consistency_model')}</strong>
        </div>
        <div style={{ fontSize: '11.5px', color: 'var(--text-secondary)', lineHeight: 1.5 }}>
          <div style={{ display: 'flex', gap: '0.4rem', alignItems: 'baseline' }}>
            <span style={{ color: 'var(--text)', fontWeight: 500 }}>{t('settings.db_sync_raft_consensus')}</span>
            <span>{t('settings.db_sync_raft_consensus_scope')}</span>
          </div>
          <div style={{ display: 'flex', gap: '0.4rem', alignItems: 'baseline' }}>
            <span style={{ color: 'var(--text)', fontWeight: 500 }}>{t('settings.db_sync_crdt_merge')}</span>
            <span>{t('settings.db_sync_crdt_merge_scope')}</span>
          </div>
        </div>
      </div>
      <div className="setting-row" style={{ borderBottom: 'none' }}>
        <div className="setting-label">
          <strong>{t('settings.db_sync_cluster_status')}</strong>
        </div>
        <div style={{ display: 'grid', gap: '0.3rem', fontSize: '11.5px', textAlign: 'right' }}>
          <div style={{ display: 'flex', gap: '0.8rem', alignItems: 'center', justifyContent: 'flex-end' }}>
            <span style={{ color: 'var(--accent)', fontWeight: 500 }}>
              {clusterStatus.enabled ? (clusterStatus.role === 'standalone' ? t('settings.db_sync_role_standalone') : clusterStatus.role) : t('settings.db_sync_role_disabled')}
            </span>
            <span style={{ color: 'var(--text-secondary)' }}>Term {clusterStatus.term}</span>
            <span style={{ color: 'var(--text-secondary)' }}>Commit #{clusterStatus.commit_index}</span>
          </div>
          <div style={{ display: 'flex', gap: '0.8rem', alignItems: 'center', justifyContent: 'flex-end' }}>
            <span style={{ color: 'var(--ok)' }}>
              {t('settings.db_sync_peers_connected', { connected: connectedPeers, total: clusterStatus.peers.length })}
            </span>
            {unreachablePeers > 0 ? <span style={{ color: 'var(--danger)' }}>{t('settings.db_sync_peers_unreachable', { count: unreachablePeers })}</span> : null}
            <span style={{ color: 'var(--text-secondary)' }}>{t('settings.db_sync_last_sync')} {formatLastSync(clusterStatus.last_sync_at)}</span>
            <span style={{ color: 'var(--text-secondary)' }}>{t('settings.db_sync_journal')} {clusterStatus.journal_entries}</span>
          </div>
          <div style={{ display: 'flex', justifyContent: 'flex-end', color: 'var(--text-secondary)' }}>
            <span>{t('settings.db_sync_lag_detail_note')}</span>
          </div>
          <div style={{ display: 'flex', justifyContent: 'flex-end', gap: '0.5rem' }}>
            <button className="btn-secondary" type="button" onClick={onOpenSyncTopology}>
              {t('settings.db_sync_view_topology')}
            </button>
            <button className="btn-secondary" type="button" onClick={onTriggerSyncNow}>
              {t('settings.db_sync_trigger_now')}
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
