import { useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import type { ClusterStatus, ClusterPeerStatus } from '../../shared/types';
import { useTranslation } from 'react-i18next';
import i18next from 'i18next';
import { useSettingsContext } from '../../state/context';
import { useSyncTopology } from '../../state/useSyncTopology';

interface SyncTopologyDialogProps {
  open: boolean;
  status: ClusterStatus | null;
  onClose: () => void;
}

export function ConnectedSyncTopologyDialog() {
  const settings = useSettingsContext();
  const topology = useSyncTopology(settings.syncTopologyDialogOpen);
  return (
    <SyncTopologyDialog
      open={settings.syncTopologyDialogOpen}
      status={topology.status}
      onClose={settings.closeSyncTopology}
    />
  );
}

function fmt(n: number): string {
  return n.toLocaleString();
}

function fmtFloat(n: number): string {
  return n.toFixed(2);
}

function heartbeatDisplay(peer: ClusterPeerStatus): string {
  if (!peer.last_heartbeat_at) return '—';
  const seconds = Math.round((Date.now() - peer.last_heartbeat_at) / 1000);
  return i18next.t('common.time_ago_seconds', { seconds });
}

function probeDisplay(peer: ClusterPeerStatus): string {
  if (peer.last_probe_duration_ms === null) return '—';
  return `${peer.last_probe_duration_ms}ms`;
}

function formatSince(ts: number | null): string {
  if (ts === null) return i18next.t('common.never');
  const deltaSeconds = Math.max(0, Math.floor((Date.now() - ts) / 1000));
  if (deltaSeconds < 60) return i18next.t('common.time_ago_seconds', { seconds: deltaSeconds });
  const minutes = Math.floor(deltaSeconds / 60);
  if (minutes < 60) return i18next.t('common.time_ago_minutes', { minutes });
  const hours = Math.floor(minutes / 60);
  return i18next.t('common.time_ago_hours', { hours });
}

function roleLabel(role: string): string {
  if (!role) return i18next.t('common.unknown');
  return role.charAt(0).toUpperCase() + role.slice(1);
}

interface StatRow {
  key: string;
  value: string;
  tone?: 'ok' | 'warn' | 'danger' | 'accent';
}

export function SyncTopologyDialog({ open, status, onClose }: SyncTopologyDialogProps) {
  const { t } = useTranslation();
  const [activeTab, setActiveTab] = useState<'overview' | 'stats'>('overview');
  const leaderRef = useRef<HTMLDivElement>(null);
  const followerRefs = useRef<(HTMLDivElement | null)[]>([]);
  const svgRef = useRef<SVGSVGElement>(null);
  const areaRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!open) setActiveTab('overview');
  }, [open]);

  // Draw SVG lines after layout
  useLayoutEffect(() => {
    if (activeTab !== 'overview' || !open || !svgRef.current || !leaderRef.current || !areaRef.current) return;

    const svgEl = svgRef.current;
    const areaRect = areaRef.current.getBoundingClientRect();

    const leaderRect = leaderRef.current.getBoundingClientRect();
    const leaderCx = leaderRect.left - areaRect.left + leaderRect.width / 2;
    const leaderCy = leaderRect.top - areaRect.top + leaderRect.height;

    // Remove old lines
    while (svgEl.firstChild) svgEl.removeChild(svgEl.firstChild);

    followerRefs.current.forEach((ref) => {
      if (!ref) return;
      const rect = ref.getBoundingClientRect();
      const cx = rect.left - areaRect.left + rect.width / 2;
      const cy = rect.top - areaRect.top;

      const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
      line.setAttribute('x1', String(leaderCx));
      line.setAttribute('y1', String(leaderCy));
      line.setAttribute('x2', String(cx));
      line.setAttribute('y2', String(cy));
      line.setAttribute('stroke', 'rgba(255,255,255,0.05)');
      line.setAttribute('stroke-width', '1');
      line.setAttribute('fill', 'none');
      svgEl.appendChild(line);
    });
  });

  const localId = status?.site_id ?? '—';
  const leaderId = status?.leader_id ?? null;
  const term = status?.term ?? 0;
  const commitIndex = status?.commit_index ?? 0;
  const journalEntries = status?.journal_entries ?? 0;
  const peers = status?.peers ?? [];
  const connectedPeers = peers.filter((peer) => peer.state === 'connected').length;
  const unreachablePeers = peers.filter((peer) => peer.state === 'unreachable').length;
  const localRole = status?.role ?? 'standalone';
  const localRoleClass = localRole === 'leader' ? 'leader' : localRole === 'candidate' ? 'candidate' : 'follower';

  const clusterStats = useMemo<StatRow[]>(
    () => [
      { key: t('settings.sync_site_label'), value: localId, tone: 'accent' },
      { key: t('settings.sync_cluster_name_label'), value: status?.cluster_name ?? '—' },
      { key: t('settings.sync_role_label'), value: roleLabel(localRole), tone: localRole === 'leader' ? 'accent' : undefined },
      { key: t('settings.sync_leader_label_colon'), value: leaderId ?? localId },
      { key: t('settings.sync_term_label_colon'), value: fmt(term) },
      { key: t('settings.sync_commit_index_label'), value: fmt(commitIndex) },
      { key: t('settings.sync_journal_entries_label'), value: fmt(journalEntries) },
      { key: t('settings.sync_pending_raft_label'), value: fmt(status?.pending_raft_entries ?? 0) },
      { key: t('settings.sync_peers_connected_label'), value: `${connectedPeers}/${peers.length}`, tone: connectedPeers > 0 ? 'ok' : undefined },
      { key: t('settings.sync_peers_unreachable_label'), value: fmt(unreachablePeers), tone: unreachablePeers > 0 ? 'danger' : 'ok' },
      { key: t('settings.sync_last_sync_label'), value: formatSince(status?.last_sync_at ?? null) },
    ],
    [commitIndex, connectedPeers, journalEntries, leaderId, localId, localRole, peers.length, status, term, unreachablePeers],
  );

  const lagStats = useMemo<StatRow[]>(
    () => [
      { key: t('settings.sync_lagging_peers_label'), value: fmt(status?.replication_lagging_peers ?? 0), tone: (status?.replication_lagging_peers ?? 0) > 0 ? 'warn' : 'ok' },
      { key: t('settings.sync_total_lag_label'), value: fmt(status?.replication_lag_total_entries ?? 0) },
      { key: t('settings.sync_max_lag_label'), value: fmt(status?.replication_lag_max_entries ?? 0) },
      { key: t('settings.sync_avg_lag_label'), value: fmt(status?.replication_lag_avg_entries ?? 0) },
      { key: t('settings.sync_ewma_lag_label'), value: fmtFloat(status?.replication_lag_ewma_entries ?? 0) },
      { key: t('settings.sync_ewma_lag_samples_label'), value: fmt(status?.replication_lag_ewma_samples ?? 0) },
      { key: t('settings.sync_alert_threshold_label'), value: fmt(status?.replication_lag_alert_threshold_entries ?? 0) },
      { key: t('settings.sync_alert_streak_label'), value: fmt(status?.replication_lag_alert_streak ?? 0) },
      { key: t('settings.sync_alert_sustain_refreshes_label'), value: fmt(status?.replication_lag_alert_sustained_refreshes ?? 0) },
      {
        key: t('settings.sync_lag_alert_state_label'),
        value: status?.replication_lag_alert_active ? i18next.t('dialogs.sync_topology_stat_active') : i18next.t('dialogs.sync_topology_stat_clear'),
        tone: status?.replication_lag_alert_active ? 'danger' : 'ok',
      },
      { key: t('settings.sync_last_alert_label'), value: formatSince(status?.replication_lag_last_alert_at ?? null) },
    ],
    [status],
  );

  const ingressStats = useMemo<StatRow[]>(
    () => [
      { key: t('settings.sync_accepted_connections_label'), value: fmt(status?.ingress_socket_accepted_connections ?? 0) },
      { key: t('settings.sync_completed_connections_label'), value: fmt(status?.ingress_socket_completed_connections ?? 0) },
      { key: t('settings.sync_failed_connections_label'), value: fmt(status?.ingress_socket_failed_connections ?? 0), tone: (status?.ingress_socket_failed_connections ?? 0) > 0 ? 'warn' : 'ok' },
      { key: t('settings.sync_active_connections_label'), value: fmt(status?.ingress_socket_active_connections ?? 0) },
      { key: t('settings.sync_peak_active_label'), value: fmt(status?.ingress_socket_peak_active_connections ?? 0) },
      { key: t('settings.sync_accept_failures_label'), value: fmt(status?.ingress_socket_accept_failures ?? 0), tone: (status?.ingress_socket_accept_failures ?? 0) > 0 ? 'warn' : 'ok' },
      { key: t('settings.sync_tls_handshake_failures_label'), value: fmt(status?.ingress_socket_tls_handshake_failures ?? 0), tone: (status?.ingress_socket_tls_handshake_failures ?? 0) > 0 ? 'warn' : 'ok' },
      { key: t('settings.sync_read_failures_label'), value: fmt(status?.ingress_socket_read_failures ?? 0), tone: (status?.ingress_socket_read_failures ?? 0) > 0 ? 'warn' : 'ok' },
      { key: t('settings.sync_apply_failures_label'), value: fmt(status?.ingress_socket_apply_failures ?? 0), tone: (status?.ingress_socket_apply_failures ?? 0) > 0 ? 'warn' : 'ok' },
      { key: t('settings.sync_hb_ack_failures_label'), value: fmt(status?.ingress_socket_handshake_ack_failures ?? 0), tone: (status?.ingress_socket_handshake_ack_failures ?? 0) > 0 ? 'warn' : 'ok' },
      { key: t('settings.sync_bytes_read_label'), value: `${fmt(status?.ingress_socket_bytes_read ?? 0)} B` },
      { key: t('settings.sync_connection_duration_label'), value: `${fmt(status?.ingress_socket_total_connection_duration_ms ?? 0)} ms` },
      { key: t('settings.sync_connection_duration_last_label'), value: `${fmt(status?.ingress_socket_last_connection_duration_ms ?? 0)} ms` },
      { key: t('settings.sync_connection_duration_max_label'), value: `${fmt(status?.ingress_socket_max_connection_duration_ms ?? 0)} ms` },
      { key: t('settings.sync_connection_duration_ewma_label'), value: `${fmtFloat(status?.ingress_socket_connection_duration_ewma_ms ?? 0)} ms` },
      { key: t('settings.sync_connection_histogram_le_10ms_label'), value: fmt(status?.ingress_socket_connection_duration_le_10ms ?? 0) },
      { key: t('settings.sync_connection_histogram_le_50ms_label'), value: fmt(status?.ingress_socket_connection_duration_le_50ms ?? 0) },
      { key: t('settings.sync_connection_histogram_le_250ms_label'), value: fmt(status?.ingress_socket_connection_duration_le_250ms ?? 0) },
      { key: t('settings.sync_connection_histogram_le_1000ms_label'), value: fmt(status?.ingress_socket_connection_duration_le_1000ms ?? 0) },
      { key: t('settings.sync_connection_histogram_gt_1000ms_label'), value: fmt(status?.ingress_socket_connection_duration_gt_1000ms ?? 0) },
      { key: t('settings.sync_max_buffered_bytes_label'), value: `${fmt(status?.ingress_socket_max_buffered_bytes ?? 0)} B` },
      { key: t('settings.sync_max_queued_frames_label'), value: fmt(status?.ingress_socket_max_queued_frames ?? 0) },
      { key: t('settings.sync_max_queued_payload_bytes_label'), value: `${fmt(status?.ingress_socket_max_queued_payload_bytes ?? 0)} B` },
      { key: t('settings.sync_paused_read_cycles_label'), value: fmt(status?.ingress_socket_paused_read_cycles ?? 0) },
      { key: t('settings.sync_paused_read_sleep_label'), value: `${fmt(status?.ingress_socket_paused_read_sleep_ms ?? 0)} ms` },
      { key: t('settings.sync_last_connection_label'), value: formatSince(status?.ingress_socket_last_connection_at ?? null) },
      { key: t('settings.sync_last_failure_label'), value: formatSince(status?.ingress_socket_last_failure_at ?? null) },
      { key: t('settings.sync_last_failure_error_label'), value: status?.ingress_socket_last_failure_error ?? i18next.t('common.none_value'), tone: status?.ingress_socket_last_failure_error ? 'warn' : 'ok' },
    ],
    [status],
  );

  const peerStats = useMemo(
    () =>
      peers.map((peer) => {
        const rows: StatRow[] = [
          { key: t('settings.sync_address_label'), value: peer.address },
          {
            key: t('settings.sync_state_label'),
            value: peer.state,
            tone: peer.state === 'connected' ? 'ok' : peer.state === 'unreachable' ? 'danger' : 'warn',
          },
          { key: t('settings.sync_role_label'), value: roleLabel(peer.role) },
          { key: t('settings.sync_discovery_source_label'), value: peer.discovered_via },
          { key: t('settings.sync_match_index_label'), value: fmt(peer.match_index) },
          {
            key: t('settings.sync_replication_lag_entries_label'),
            value: fmt(peer.replication_lag_entries),
            tone: peer.replication_lag_entries > 0 ? 'warn' : 'ok',
          },
          { key: t('settings.sync_heartbeat_failures_label'), value: fmt(peer.consecutive_heartbeat_failures) },
          { key: t('settings.sync_probe_failures_label'), value: fmt(peer.consecutive_probe_failures) },
          { key: t('settings.sync_last_heartbeat_label'), value: heartbeatDisplay(peer) },
          { key: t('settings.sync_last_probe_label'), value: formatSince(peer.last_probe_at) },
          { key: t('settings.sync_last_probe_duration_label'), value: probeDisplay(peer) },
          { key: t('settings.sync_last_probe_error_label'), value: peer.last_probe_error ?? i18next.t('common.none_value'), tone: peer.last_probe_error ? 'warn' : 'ok' },
          { key: t('settings.sync_ingress_accepted_batches_label'), value: fmt(peer.ingress_accepted_batches) },
          { key: t('settings.sync_ingress_rejected_batches_label'), value: fmt(peer.ingress_rejected_batches), tone: peer.ingress_rejected_batches > 0 ? 'warn' : 'ok' },
          { key: t('settings.sync_ingress_accepted_wire_bytes_label'), value: `${fmt(peer.ingress_accepted_wire_bytes)} B` },
          { key: t('settings.sync_ingress_rejected_wire_bytes_label'), value: `${fmt(peer.ingress_rejected_wire_bytes)} B` },
          { key: t('settings.sync_ingress_last_wire_batch_label'), value: `${fmt(peer.ingress_last_wire_batch_bytes)} B` },
          { key: t('settings.sync_ingress_apply_duration_total_label'), value: `${fmt(peer.ingress_total_apply_duration_ms)} ms` },
          { key: t('settings.sync_ingress_apply_duration_last_label'), value: `${fmt(peer.ingress_last_apply_duration_ms)} ms` },
          { key: t('settings.sync_ingress_apply_duration_max_label'), value: `${fmt(peer.ingress_max_apply_duration_ms)} ms` },
          { key: t('settings.sync_ingress_apply_duration_ewma_label'), value: `${fmtFloat(peer.ingress_apply_duration_ewma_ms)} ms` },
          { key: t('settings.sync_ingress_apply_duration_samples_label'), value: fmt(peer.ingress_apply_duration_samples) },
          { key: t('settings.sync_ingress_replication_latency_total_label'), value: `${fmt(peer.ingress_total_replication_latency_ms)} ms` },
          { key: t('settings.sync_ingress_replication_latency_last_label'), value: `${fmt(peer.ingress_last_replication_latency_ms)} ms` },
          { key: t('settings.sync_ingress_replication_latency_max_label'), value: `${fmt(peer.ingress_max_replication_latency_ms)} ms` },
          { key: t('settings.sync_ingress_replication_latency_ewma_label'), value: `${fmtFloat(peer.ingress_replication_latency_ewma_ms)} ms` },
          { key: t('settings.sync_ingress_replication_latency_samples_label'), value: fmt(peer.ingress_replication_latency_samples) },
          { key: t('settings.sync_ingress_inflight_wire_batches_label'), value: fmt(peer.ingress_inflight_wire_batches) },
          { key: t('settings.sync_ingress_inflight_wire_batches_peak_label'), value: fmt(peer.ingress_inflight_wire_batches_peak) },
          { key: t('settings.sync_ingress_inflight_wire_bytes_label'), value: `${fmt(peer.ingress_inflight_wire_bytes)} B` },
          { key: t('settings.sync_ingress_inflight_wire_bytes_peak_label'), value: `${fmt(peer.ingress_inflight_wire_bytes_peak)} B` },
          { key: t('settings.sync_ingress_rejected_batch_too_large_label'), value: fmt(peer.ingress_rejected_batch_too_large), tone: peer.ingress_rejected_batch_too_large > 0 ? 'warn' : 'ok' },
          { key: t('settings.sync_ingress_rejected_backpressure_label'), value: fmt(peer.ingress_rejected_backpressure), tone: peer.ingress_rejected_backpressure > 0 ? 'warn' : 'ok' },
          { key: t('settings.sync_ingress_rejected_inflight_budget_label'), value: fmt(peer.ingress_rejected_inflight_wire_budget), tone: peer.ingress_rejected_inflight_wire_budget > 0 ? 'warn' : 'ok' },
          { key: t('settings.sync_ingress_rejected_handshake_auth_label'), value: fmt(peer.ingress_rejected_handshake_auth), tone: peer.ingress_rejected_handshake_auth > 0 ? 'warn' : 'ok' },
          { key: t('settings.sync_ingress_rejected_handshake_schema_label'), value: fmt(peer.ingress_rejected_handshake_schema), tone: peer.ingress_rejected_handshake_schema > 0 ? 'warn' : 'ok' },
          { key: t('settings.sync_ingress_rejected_invalid_wire_batch_label'), value: fmt(peer.ingress_rejected_invalid_wire_batch), tone: peer.ingress_rejected_invalid_wire_batch > 0 ? 'warn' : 'ok' },
          { key: t('settings.sync_ingress_rejected_entry_limit_label'), value: fmt(peer.ingress_rejected_entry_limit), tone: peer.ingress_rejected_entry_limit > 0 ? 'warn' : 'ok' },
          { key: t('settings.sync_ingress_rejected_rate_limit_label'), value: fmt(peer.ingress_rejected_rate_limit), tone: peer.ingress_rejected_rate_limit > 0 ? 'warn' : 'ok' },
          { key: t('settings.sync_ingress_rejected_apply_batch_label'), value: fmt(peer.ingress_rejected_apply_batch), tone: peer.ingress_rejected_apply_batch > 0 ? 'warn' : 'ok' },
          { key: t('settings.sync_ingress_rejected_ingress_protocol_label'), value: fmt(peer.ingress_rejected_ingress_protocol), tone: peer.ingress_rejected_ingress_protocol > 0 ? 'warn' : 'ok' },
          { key: t('settings.sync_ingress_last_rejection_label'), value: formatSince(peer.last_ingress_rejection_at) },
          { key: t('settings.sync_ingress_last_rejection_reason_label'), value: peer.last_ingress_rejection_reason ?? i18next.t('common.none_value'), tone: peer.last_ingress_rejection_reason ? 'warn' : 'ok' },
          { key: t('settings.sync_ingress_last_rejection_error_label'), value: peer.last_ingress_rejection_error ?? i18next.t('common.none_value'), tone: peer.last_ingress_rejection_error ? 'warn' : 'ok' },
        ];
        return { peer, rows };
      }),
    [peers],
  );

  const noStatusRows: StatRow[] = [{ key: 'Status', value: 'No cluster telemetry available yet.', tone: 'warn' }];

  function renderRows(rows: StatRow[]) {
    return (
      <dl className="sync-stats-kv">
        {rows.map((row) => (
          <div className="sync-stats-kv-row" key={row.key}>
            <dt>{row.key}</dt>
            <dd className={row.tone ? `sync-stat-tone-${row.tone}` : undefined}>{row.value}</dd>
          </div>
        ))}
      </dl>
    );
  }

  if (!open) return null;

  return (
    <dialog
      open
      id="syncTopologyDialog"
      onClick={(e) => e.currentTarget === e.target && onClose()}
    >
      <header className="sync-popup-header">
        <div className="sync-popup-header-left">
          <span className="eyebrow">Cluster</span>
          <h3>Synchronization</h3>
        </div>
        <div className="sync-popup-meta">
          <span>
            Leader{' '}
            <strong className="accent-val">{leaderId ?? localId}</strong>
          </span>
          <span>
            Term <strong>{fmt(term)}</strong>
          </span>
          <span>
            Commit <strong>#{fmt(commitIndex)}</strong>
          </span>
        </div>
        <button className="dialog-close" type="button" aria-label="Close" onClick={onClose}>
          &times;
        </button>
      </header>

      <div className="sync-popup-tabs" role="tablist" aria-label="Cluster detail tabs">
        <button
          className={`sync-popup-tab${activeTab === 'overview' ? ' active' : ''}`}
          type="button"
          role="tab"
          aria-selected={activeTab === 'overview'}
          onClick={() => setActiveTab('overview')}
        >
          Overview
        </button>
        <button
          className={`sync-popup-tab${activeTab === 'stats' ? ' active' : ''}`}
          type="button"
          role="tab"
          aria-selected={activeTab === 'stats'}
          onClick={() => setActiveTab('stats')}
        >
          Stats
        </button>
      </div>

      {activeTab === 'overview' ? (
        <div className="sync-topology-area" ref={areaRef} role="tabpanel" aria-label="Cluster overview">
          <svg className="sync-topology-svg" ref={svgRef} />

          <div className="sync-nodes-layer">
            <div className={`sync-node-card ${localRoleClass === 'leader' ? 'leader' : ''}`} ref={leaderRef}>
              <div className="sync-node-header">
                <span className="sync-node-site-id">{localId}</span>
                <span className={`sync-role-badge ${localRoleClass}`}>{roleLabel(localRole)}</span>
              </div>
              <div className="sync-node-stats">
                <div className="sync-node-stat">
                  <span className="label">Commit</span>
                  <span className="value synced">{fmt(commitIndex)}</span>
                </div>
                <div className="sync-node-stat">
                  <span className="label">Applied</span>
                  <span className="value">{fmt(commitIndex)}</span>
                </div>
                <div className="sync-node-stat">
                  <span className="label">Term</span>
                  <span className="value">{fmt(term)}</span>
                </div>
                <div className="sync-node-stat">
                  <span className="label">Log</span>
                  <span className="value">{fmt(journalEntries)}</span>
                </div>
              </div>
            </div>

            {peers.length > 0 && (
              <div className="sync-follower-row">
                {peers.map((peer, i) => {
                  const lagClass = peer.replication_lag_entries > 0 ? 'lag' : 'synced';
                  const roleBadge = peer.role === 'leader' ? 'leader' : peer.role === 'candidate' ? 'candidate' : 'follower';
                  return (
                    <div
                      key={`${peer.site_id}-${peer.address}`}
                      className="sync-node-card"
                      ref={(el) => {
                        followerRefs.current[i] = el;
                      }}
                    >
                      <div className="sync-node-header">
                        <span className="sync-node-site-id">{peer.site_id}</span>
                        <span className={`sync-role-badge ${roleBadge}`}>{roleLabel(peer.role)}</span>
                      </div>
                      <div className="sync-node-stats">
                        <div className="sync-node-stat">
                          <span className="label">Match</span>
                          <span className={`value ${lagClass}`}>{fmt(peer.match_index)}</span>
                        </div>
                        <div className="sync-node-stat">
                          <span className="label">Lag</span>
                          <span className={`value ${lagClass}`}>{fmt(peer.replication_lag_entries)}</span>
                        </div>
                        <div className="sync-node-stat">
                          <span className="label">Heartbeat</span>
                          <span className="value">{heartbeatDisplay(peer)}</span>
                        </div>
                        <div className="sync-node-stat">
                          <span className="label">Probe</span>
                          <span className="value">{probeDisplay(peer)}</span>
                        </div>
                      </div>
                    </div>
                  );
                })}
              </div>
            )}
          </div>
        </div>
      ) : (
        <div className="sync-stats-area" role="tabpanel" aria-label="Cluster stats">
          <section className="sync-stats-group">
            <h4>Cluster</h4>
            {renderRows(status ? clusterStats : noStatusRows)}
          </section>
          <section className="sync-stats-group">
            <h4>Replication Lag</h4>
            {renderRows(status ? lagStats : noStatusRows)}
          </section>
          <section className="sync-stats-group">
            <h4>Ingress Socket</h4>
            {renderRows(status ? ingressStats : noStatusRows)}
          </section>
          <section className="sync-stats-group">
            <h4>Peers</h4>
            {peerStats.length === 0 ? (
              <p className="sync-stats-empty">No discovered peers.</p>
            ) : (
              <div className="sync-peer-stats-grid">
                {peerStats.map(({ peer, rows }) => (
                  <article className="sync-stats-peer-card" key={`${peer.site_id}-${peer.address}`}>
                    <header className="sync-stats-peer-header">
                      <strong className="sync-node-site-id">{peer.site_id}</strong>
                      <span className={`sync-role-badge ${peer.role === 'leader' ? 'leader' : peer.role === 'candidate' ? 'candidate' : 'follower'}`}>
                        {roleLabel(peer.role)}
                      </span>
                    </header>
                    {renderRows(rows)}
                  </article>
                ))}
              </div>
            )}
          </section>
        </div>
      )}

      {activeTab === 'overview' ? (
        <div className="sync-popup-footer">
          <div className="sync-legend-item">
            <div className="sync-legend-swatch" style={{ background: 'var(--accent)' }} />
            <span>Replicating down</span>
          </div>
          <div className="sync-legend-item">
            <div className="sync-legend-swatch" style={{ background: '#8a9fd4' }} />
            <span>Replicating up</span>
          </div>
          <div className="sync-legend-item">
            <div className="sync-legend-swatch" style={{ background: 'var(--warn)' }} />
            <span>Lagging</span>
          </div>
          <div className="sync-legend-item">
            <div className="sync-legend-swatch" style={{ background: 'var(--ok)' }} />
            <span>Synced</span>
          </div>
        </div>
      ) : null}
    </dialog>
  );
}
