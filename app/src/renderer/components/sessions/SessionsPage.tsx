import type { Account, StickySession } from '../../shared/types';

interface SessionsPageProps {
  visible: boolean;
  accounts: Account[];
  sessionsKindFilter: StickySession['kind'] | 'all';
  sessionsView: { filtered: StickySession[]; paged: StickySession[]; staleTotal: number };
  paginationLabel: string;
  canPrev: boolean;
  canNext: boolean;
  onSetKindFilter: (kind: StickySession['kind'] | 'all') => void;
  onPrevPage: () => void;
  onNextPage: () => void;
  onPurgeStale: () => void;
}

const KIND_LABELS: Record<StickySession['kind'], string> = {
  codex_session: 'Codex session',
  sticky_thread: 'Sticky thread',
  prompt_cache: 'Prompt cache',
};

export function SessionsPage({
  visible,
  accounts,
  sessionsKindFilter,
  sessionsView,
  paginationLabel,
  canPrev,
  canNext,
  onSetKindFilter,
  onPrevPage,
  onNextPage,
  onPurgeStale,
}: SessionsPageProps) {
  if (!visible) return null;

  return (
    <section className="sessions-page page active" id="pageSessions" data-page="sessions">
      <div className="sessions-content">
        <header className="section-header">
          <div>
            <p className="eyebrow">Sticky</p>
            <h2>Sessions</h2>
          </div>
          <button className="dock-btn accent" id="purgeStaleSessions" type="button" onClick={onPurgeStale}>
            Purge stale
          </button>
        </header>
        <div className="sessions-stats">
          <span>
            <span className="stat-label">Visible rows</span>
            <strong>{sessionsView.filtered.length}</strong>
          </span>
          <span>
            <span className="stat-label">Stale prompt-cache</span>
            <strong>{sessionsView.staleTotal}</strong>
          </span>
        </div>
        <div className="sessions-filter">
          <button className={`filter-btn${sessionsKindFilter === 'all' ? ' active' : ''}`} data-kind="all" type="button" onClick={() => onSetKindFilter('all')}>
            All
          </button>
          <button
            className={`filter-btn${sessionsKindFilter === 'codex_session' ? ' active' : ''}`}
            data-kind="codex_session"
            type="button"
            onClick={() => onSetKindFilter('codex_session')}
          >
            Codex session
          </button>
          <button
            className={`filter-btn${sessionsKindFilter === 'sticky_thread' ? ' active' : ''}`}
            data-kind="sticky_thread"
            type="button"
            onClick={() => onSetKindFilter('sticky_thread')}
          >
            Sticky thread
          </button>
          <button
            className={`filter-btn${sessionsKindFilter === 'prompt_cache' ? ' active' : ''}`}
            data-kind="prompt_cache"
            type="button"
            onClick={() => onSetKindFilter('prompt_cache')}
          >
            Prompt cache
          </button>
        </div>
        <div className="table-wrap">
          <table>
            <thead>
              <tr>
                <th>Key</th>
                <th>Kind</th>
                <th>Account</th>
                <th>Updated</th>
                <th>Expiry</th>
                <th />
              </tr>
            </thead>
            <tbody>
              {sessionsView.paged.length === 0 ? (
                <tr>
                  <td colSpan={6} className="empty-state">
                    No sticky sessions match the current filter.
                  </td>
                </tr>
              ) : (
                sessionsView.paged.map((session) => {
                  const accountName = accounts.find((account) => account.id === session.accountId)?.name ?? session.accountId;
                  return (
                    <tr key={session.key}>
                      <td
                        className="mono"
                        style={{ fontSize: '11px', maxWidth: '16rem', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}
                        title={session.key}
                      >
                        {session.key}
                      </td>
                      <td>
                        <span className={`kind-badge ${session.kind.replace('_', '-')}`}>{KIND_LABELS[session.kind]}</span>
                      </td>
                      <td style={{ fontSize: '12px' }}>{accountName}</td>
                      <td style={{ fontSize: '12px', color: 'var(--text-secondary)' }}>{session.updated}</td>
                      <td style={{ fontSize: '12px', color: 'var(--text-secondary)' }}>
                        {session.stale ? (
                          <>
                            {session.expiry}
                            <span className="stale-badge">Stale</span>
                          </>
                        ) : session.expiry ? (
                          session.expiry
                        ) : (
                          <span className="durable-label">Durable</span>
                        )}
                      </td>
                      <td style={{ textAlign: 'right' }}>
                        <button className="btn-danger" style={{ fontSize: '11px', padding: '0.15rem 0.4rem' }} type="button">
                          Remove
                        </button>
                      </td>
                    </tr>
                  );
                })
              )}
            </tbody>
          </table>
        </div>
        <div className="sessions-pagination">
          <span>{paginationLabel}</span>
          <button type="button" disabled={!canPrev} onClick={onPrevPage}>
            &lsaquo; Prev
          </button>
          <button type="button" disabled={!canNext} onClick={onNextPage}>
            Next &rsaquo;
          </button>
        </div>
      </div>
    </section>
  );
}
