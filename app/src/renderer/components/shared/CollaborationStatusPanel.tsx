import { MessageSquare, Monitor, Network, Users } from 'lucide-react';
import { useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import type { ClusterPeerState, ClusterStatus, StickySession } from '../../shared/types';

export interface AccountSessionSummary {
  active: number;
  stale: number;
  codex: number;
  sticky: number;
  promptCache: number;
}

const EMPTY_ACCOUNT_SESSION_SUMMARY: AccountSessionSummary = {
  active: 0,
  stale: 0,
  codex: 0,
  sticky: 0,
  promptCache: 0,
};

export function emptyAccountSessionSummary(): AccountSessionSummary {
  return { ...EMPTY_ACCOUNT_SESSION_SUMMARY };
}

export function buildAccountSessionSummaries(sessions: StickySession[]): Map<string, AccountSessionSummary> {
  const summaries = new Map<string, AccountSessionSummary>();
  for (const session of sessions) {
    if (!session.accountId) {
      continue;
    }
    const summary = summaries.get(session.accountId) ?? emptyAccountSessionSummary();
    if (session.stale) {
      summary.stale += 1;
    } else {
      summary.active += 1;
      if (session.kind === 'codex_session') {
        summary.codex += 1;
      } else if (session.kind === 'prompt_cache') {
        summary.promptCache += 1;
      } else {
        summary.sticky += 1;
      }
    }
    summaries.set(session.accountId, summary);
  }
  return summaries;
}

function normalizeSummary(summary: AccountSessionSummary | null | undefined): AccountSessionSummary {
  return summary ?? EMPTY_ACCOUNT_SESSION_SUMMARY;
}

function peerStateTone(state: ClusterPeerState): 'connected' | 'disconnected' | 'unreachable' {
  if (state === 'connected' || state === 'unreachable') {
    return state;
  }
  return 'disconnected';
}

interface AccountSessionChipProps {
  summary: AccountSessionSummary | null | undefined;
  className?: string;
}

export function AccountSessionChip({ summary, className = '' }: AccountSessionChipProps) {
  const { t } = useTranslation();
  const normalized = normalizeSummary(summary);
  if (normalized.active <= 0) {
    return null;
  }
  const isParallel = normalized.codex >= 2;
  const title = isParallel
    ? t('collaboration.parallel_session_title', { count: normalized.codex })
    : t('collaboration.account_sessions_title', { count: normalized.active });

  return (
    <span className={`account-session-chip${isParallel ? ' parallel' : ''}${className ? ` ${className}` : ''}`} title={title}>
      <MessageSquare size={11} aria-hidden="true" />
      <span>{normalized.active}</span>
    </span>
  );
}

interface CollaborationStatusPanelProps {
  accountsTotal: number;
  sessions: StickySession[];
  clusterStatus: ClusterStatus | null | undefined;
  accountSessionSummary?: AccountSessionSummary | null;
  selectedAccountName?: string | null;
  variant?: 'pool' | 'compact' | 'detail';
  onOpenSyncTopology?: () => void;
}

export function CollaborationStatusPanel({
  accountsTotal,
  sessions,
  clusterStatus,
  accountSessionSummary,
  selectedAccountName = null,
  variant = 'pool',
  onOpenSyncTopology,
}: CollaborationStatusPanelProps) {
  const { t } = useTranslation();
  const activeSessions = useMemo(() => sessions.filter((session) => !session.stale), [sessions]);
  const activeCodexSessions = useMemo(
    () => activeSessions.filter((session) => session.kind === 'codex_session'),
    [activeSessions],
  );
  const activeStickySessions = useMemo(
    () => activeSessions.filter((session) => session.kind === 'sticky_thread'),
    [activeSessions],
  );
  const staleSessions = sessions.length - activeSessions.length;
  const peers = clusterStatus?.enabled ? clusterStatus.peers : [];
  const connectedPeers = peers.filter((peer) => peer.state === 'connected').length;
  const totalDevices = clusterStatus?.enabled ? peers.length + 1 : 1;
  const connectedDevices = clusterStatus?.enabled ? connectedPeers + 1 : 1;
  const laggingPeers = clusterStatus?.replication_lagging_peers ?? 0;
  const selectedSummary = normalizeSummary(accountSessionSummary);
  const showSelectedAccount = selectedAccountName !== null;
  const visiblePeers = peers.slice(0, 4);
  const hiddenPeers = Math.max(0, peers.length - visiblePeers.length);
  const localSite = clusterStatus?.site_id?.trim() || t('collaboration.local_site_unknown');

  return (
    <section className={`collaboration-panel ${variant}`} aria-label={t('collaboration.panel_label')}>
      <div className={`collaboration-card${activeCodexSessions.length >= 2 ? ' active' : ''}`}>
        <div className="collaboration-card-top">
          <span className="collaboration-icon" aria-hidden="true">
            <MessageSquare size={14} />
          </span>
          <span>{t('collaboration.local_chats')}</span>
        </div>
        <div className="collaboration-metric-row">
          <strong>{activeCodexSessions.length}</strong>
          <span>{t('collaboration.same_device')}</span>
        </div>
        <span className="collaboration-subline">
          {t('collaboration.local_session_mix', {
            codex: activeCodexSessions.length,
            sticky: activeStickySessions.length,
          })}
        </span>
      </div>

      <div className={`collaboration-card${connectedPeers > 0 ? ' active' : ''}${laggingPeers > 0 ? ' warn' : ''}`}>
        <div className="collaboration-card-top">
          <span className="collaboration-icon" aria-hidden="true">
            <Network size={14} />
          </span>
          <span>{t('collaboration.synced_devices')}</span>
          {onOpenSyncTopology ? (
            <button className="collaboration-topology-btn" type="button" onClick={onOpenSyncTopology}>
              {t('collaboration.topology')}
            </button>
          ) : null}
        </div>
        <div className="collaboration-metric-row">
          <strong>
            {connectedDevices}/{totalDevices}
          </strong>
          <span>{clusterStatus?.enabled ? t('collaboration.devices_connected') : t('collaboration.sync_off')}</span>
        </div>
        <div className="collaboration-node-strip" aria-label={t('collaboration.device_nodes')}>
          <span className="collaboration-node local" title={t('collaboration.local_site_title', { siteId: localSite })}>
            <Monitor size={12} aria-hidden="true" />
            <span>{localSite}</span>
          </span>
          {visiblePeers.map((peer) => {
            const tone = peerStateTone(peer.state);
            const label = t(`collaboration.peer_state_${tone}`);
            return (
              <span
                key={`${peer.site_id}-${peer.address}`}
                className={`collaboration-node peer ${tone}`}
                title={t('collaboration.peer_site_title', {
                  siteId: peer.site_id || t('collaboration.local_site_unknown'),
                  state: label,
                })}
              >
                <span className="collaboration-node-dot" aria-hidden="true" />
                <span>{peer.site_id || '?'}</span>
              </span>
            );
          })}
          {hiddenPeers > 0 ? <span className="collaboration-node more">+{hiddenPeers}</span> : null}
        </div>
      </div>

      <div className="collaboration-card">
        <div className="collaboration-card-top">
          <span className="collaboration-icon" aria-hidden="true">
            <Users size={14} />
          </span>
          <span>{t('collaboration.shared_pool')}</span>
        </div>
        <div className="collaboration-metric-row">
          <strong>{accountsTotal}</strong>
          <span>{t('collaboration.accounts_total')}</span>
        </div>
        <span className="collaboration-subline">
          {staleSessions > 0
            ? t('collaboration.stale_affinities_count', { count: staleSessions })
            : t('collaboration.no_stale_affinities')}
        </span>
      </div>

      {showSelectedAccount ? (
        <div className={`collaboration-card selected${selectedSummary.codex >= 2 ? ' active' : ''}`}>
          <div className="collaboration-card-top">
            <span className="collaboration-icon" aria-hidden="true">
              <MessageSquare size={14} />
            </span>
            <span>{t('collaboration.selected_account')}</span>
          </div>
          <div className="collaboration-metric-row">
            <strong>{selectedSummary.active}</strong>
            <span>{selectedAccountName}</span>
          </div>
          <span className="collaboration-subline">
            {t('collaboration.account_session_mix', {
              codex: selectedSummary.codex,
              sticky: selectedSummary.sticky,
              cache: selectedSummary.promptCache,
            })}
          </span>
        </div>
      ) : null}
    </section>
  );
}
