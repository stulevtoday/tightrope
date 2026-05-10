import i18next from 'i18next';
import { useTranslation } from 'react-i18next';
import { useAccountsContext, useNavigationContext, useSessionsContext } from '../../state/context';
import type { StickySession } from '../../shared/types';

const KIND_LABELS: Record<StickySession['kind'], string> = {
  codex_session: i18next.t('sessions.kind_codex_session'),
  sticky_thread: i18next.t('sessions.kind_sticky_thread'),
  prompt_cache: i18next.t('sessions.kind_prompt_cache'),
};

export function SessionsPage() {
  const { t } = useTranslation();
  const navigation = useNavigationContext();
  const accounts = useAccountsContext();
  const sessions = useSessionsContext();

  if (navigation.currentPage !== 'sessions') return null;

  return (
    <section className="sessions-page page active" id="pageSessions" data-page="sessions">
      <div className="sessions-content">
        <header className="section-header">
          <div>
            <p className="eyebrow">{t('sessions.eyebrow')}</p>
            <h2>{t('sessions.title')}</h2>
          </div>
          <button className="dock-btn accent" id="purgeStaleSessions" type="button" onClick={sessions.purgeStaleSessions}>
            {t('sessions.purge_stale')}
          </button>
        </header>
        <div className="sessions-stats">
          <span>
            <span className="stat-label">{t('sessions.visible_rows')}</span>
            <strong>{sessions.sessionsView.filtered.length}</strong>
          </span>
          <span>
            <span className="stat-label">{t('sessions.stale_prompt_cache')}</span>
            <strong>{sessions.sessionsView.staleTotal}</strong>
          </span>
        </div>
        <div className="sessions-filter">
          <button className={`filter-btn${sessions.sessionsKindFilter === 'all' ? ' active' : ''}`} data-kind="all" type="button" onClick={() => sessions.setSessionsKindFilter('all')}>
            {t('sessions.filter_all')}
          </button>
          <button
            className={`filter-btn${sessions.sessionsKindFilter === 'codex_session' ? ' active' : ''}`}
            data-kind="codex_session"
            type="button"
            onClick={() => sessions.setSessionsKindFilter('codex_session')}
          >
            {t('sessions.filter_codex_session')}
          </button>
          <button
            className={`filter-btn${sessions.sessionsKindFilter === 'sticky_thread' ? ' active' : ''}`}
            data-kind="sticky_thread"
            type="button"
            onClick={() => sessions.setSessionsKindFilter('sticky_thread')}
          >
            {t('sessions.filter_sticky_thread')}
          </button>
          <button
            className={`filter-btn${sessions.sessionsKindFilter === 'prompt_cache' ? ' active' : ''}`}
            data-kind="prompt_cache"
            type="button"
            onClick={() => sessions.setSessionsKindFilter('prompt_cache')}
          >
            {t('sessions.filter_prompt_cache')}
          </button>
        </div>
        <div className="table-wrap">
          <table>
            <thead>
              <tr>
                <th>{t('sessions.col_key')}</th>
                <th>{t('sessions.col_kind')}</th>
                <th>{t('sessions.col_account')}</th>
                <th>{t('sessions.col_updated')}</th>
                <th>{t('sessions.col_expiry')}</th>
                <th />
              </tr>
            </thead>
            <tbody>
              {sessions.sessionsView.paged.length === 0 ? (
                <tr>
                  <td colSpan={6} className="empty-state">
                    {t('sessions.no_matching')}
                  </td>
                </tr>
              ) : (
                sessions.sessionsView.paged.map((session) => {
                  const accountName = accounts.accounts.find((account) => account.id === session.accountId)?.name ?? session.accountId;
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
                            <span className="stale-badge">{t('sessions.stale_badge')}</span>
                          </>
                        ) : session.expiry ? (
                          session.expiry
                        ) : (
                          <span className="durable-label">{t('sessions.durable_label')}</span>
                        )}
                      </td>
                      <td style={{ textAlign: 'right' }}>
                        <button className="btn-danger" style={{ fontSize: '11px', padding: '0.15rem 0.4rem' }} type="button">
                          {t('sessions.remove')}
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
          <span>{sessions.sessionsPaginationLabel}</span>
          <button type="button" disabled={!sessions.canPrevSessions} onClick={sessions.prevSessionsPage}>
            &lsaquo; {t('sessions.prev')}
          </button>
          <button type="button" disabled={!sessions.canNextSessions} onClick={sessions.nextSessionsPage}>
            {t('sessions.next')} &rsaquo;
          </button>
        </div>
      </div>
    </section>
  );
}
