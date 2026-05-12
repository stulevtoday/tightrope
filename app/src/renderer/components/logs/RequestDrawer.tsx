import { useTranslation } from 'react-i18next';
import { useAccountsContext, useLogsContext, useRouterDerivedContext } from '../../state/context';
import { statusClass } from '../../state/logic';

export function RequestDrawer() {
  const { t } = useTranslation();
  const accounts = useAccountsContext();
  const logs = useLogsContext();
  const derived = useRouterDerivedContext();
  const row = logs.drawerRow;
  const open = row !== null;
  if (!row) {
    return (
      <>
        <div className="drawer-backdrop" />
        <aside className="log-drawer" />
      </>
    );
  }

  const account = accounts.accounts.find((candidate) => candidate.id === row.accountId) ?? accounts.accounts[0];
  const accountName = account?.name ?? row.accountId ?? t('drawer.unassigned');
  const scored = accounts.accounts
    .map((candidate) => {
      const metric = derived.metrics.get(candidate.id);
      return {
        name: candidate.name,
        score: metric?.score ?? Infinity,
        picked: candidate.id === row.accountId,
      };
    })
    .sort((a, b) => a.score - b.score);

  return (
    <>
      <div className={`drawer-backdrop${open ? ' open' : ''}`} onClick={logs.closeDrawer} />
      <aside className={`log-drawer${open ? ' open' : ''}`}>
        <header className="section-header">
          <div>
            <p className="eyebrow">{t('drawer.eyebrow')}</p>
            <h2>{row.id}</h2>
          </div>
          <button className="dialog-close" type="button" aria-label={t('common.close')} onClick={logs.closeDrawer}>
            &times;
          </button>
        </header>
        <div className="drawer-body">
          <div className="drawer-section">
            <div className="drawer-section-header">{t('drawer.section_request')}</div>
            <dl className="drawer-kv">
              <dt>{t('drawer.col_id')}</dt>
              <dd>{row.id}</dd>
              {row.requestedAt ? (
                <>
                  <dt>{t('drawer.col_requested_at')}</dt>
                  <dd>{row.requestedAt}</dd>
                </>
              ) : null}
              <dt>{t('drawer.col_time')}</dt>
              <dd>{row.time}</dd>
              {row.method ? (
                <>
                  <dt>{t('drawer.col_method')}</dt>
                  <dd>{row.method}</dd>
                </>
              ) : null}
              {row.path ? (
                <>
                  <dt>{t('drawer.col_path')}</dt>
                  <dd>{row.path}</dd>
                </>
              ) : null}
              <dt>{t('drawer.col_protocol')}</dt>
              <dd>{row.protocol}</dd>
              <dt>{t('drawer.col_model')}</dt>
              <dd>{row.model}</dd>
              <dt>{t('drawer.col_account')}</dt>
              <dd>{accountName}</dd>
              <dt>{t('drawer.col_tokens')}</dt>
              <dd>{accounts.formatNumber(row.tokens)}</dd>
              <dt>{t('drawer.col_latency')}</dt>
              <dd>{row.latency} ms</dd>
              {typeof row.statusCode === 'number' ? (
                <>
                  <dt>{t('drawer.col_status_code')}</dt>
                  <dd>{row.statusCode}</dd>
                </>
              ) : null}
              <dt>{t('drawer.col_status')}</dt>
              <dd>
                <span className={`status-badge ${statusClass(row.status)}`}>{t(`common.status_${row.status}`)}</span>
              </dd>
              {row.errorCode ? (
                <>
                  <dt>{t('drawer.col_error_code')}</dt>
                  <dd>{row.errorCode}</dd>
                </>
              ) : null}
              <dt>{t('drawer.col_session')}</dt>
              <dd>{row.sessionId}</dd>
            </dl>
          </div>

          <div className="drawer-section">
            <div className="drawer-section-header">{t('drawer.section_routing_decision')}</div>
            <table className="scoring-table">
              <thead>
                <tr>
                  <th>{t('drawer.col_account')}</th>
                  <th>{t('router.inspector_score')}</th>
                </tr>
              </thead>
              <tbody>
                {scored.map((entry) => (
                  <tr key={entry.name} className={entry.picked ? 'picked' : ''}>
                    <td>
                      {entry.name}
                      {entry.picked ? ` ${t('drawer.picked_arrow')}` : ''}
                    </td>
                    <td style={{ fontFamily: "'SF Mono',ui-monospace,monospace", fontSize: '11px' }}>
                      {Number.isFinite(entry.score) ? entry.score.toFixed(4) : '∞'}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>
      </aside>
    </>
  );
}
