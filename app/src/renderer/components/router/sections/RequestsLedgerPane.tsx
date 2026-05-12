import { useTranslation } from 'react-i18next';
import type { Account, RouteMetrics, RouteRow } from '../../../shared/types';
import { statusClass } from '../../../state/logic';

interface RequestsLedgerPaneProps {
  accounts: Account[];
  metrics: Map<string, RouteMetrics>;
  visibleRows: RouteRow[];
  selectedAccountId: string;
  selectedRouteId: string;
  kpis: { rpm: number; p95: number; failover: number; sticky: number };
  formatNumber: (value: number) => string;
  onSelectRoute: (row: RouteRow) => void;
}

export function RequestsLedgerPane({
  accounts,
  metrics,
  visibleRows,
  selectedAccountId,
  selectedRouteId,
  kpis,
  formatNumber,
  onSelectRoute,
}: RequestsLedgerPaneProps) {
  const { t } = useTranslation();
  return (
    <section className="pane ledger-pane">
      <header className="section-header ledger-header">
        <div>
          <p className="eyebrow">{t('router.ledger_eyebrow')}</p>
          <h2>{t('router.ledger_title')}</h2>
        </div>
        <div className="protocol-filters" aria-label={t('common.protocol_filters')}>
          <span className="filter-chip active">SSE</span>
          <span className="filter-chip active">WS</span>
          <span className="filter-chip">Compact</span>
          <span className="filter-chip">Transcribe</span>
        </div>
      </header>
      <div className="pane-body ledger-body">
        <section className="ops-strip" aria-label={t('common.key_metrics')}>
          <article className="metric">
            <span>{t('router.ledger_req_min')}</span>
            <strong>{formatNumber(kpis.rpm)}</strong>
          </article>
          <article className="metric">
            <span>{t('router.ledger_p95_latency')}</span>
            <strong>{kpis.p95} ms</strong>
          </article>
          <article className="metric">
            <span>{t('router.ledger_failovers')}</span>
            <strong>{kpis.failover}</strong>
          </article>
          <article className="metric">
            <span>{t('router.ledger_sticky_hit')}</span>
            <strong>{kpis.sticky}%</strong>
          </article>
        </section>

        <div className="table-wrap">
          <table>
            <thead>
              <tr>
                <th>{t('router.ledger_col_time')}</th>
                <th>{t('router.ledger_col_request')}</th>
                <th>{t('router.ledger_col_protocol')}</th>
                <th>{t('router.ledger_col_model')}</th>
                <th>{t('router.ledger_col_account')}</th>
                <th>{t('router.ledger_col_tokens')}</th>
                <th>{t('router.ledger_col_latency')}</th>
                <th>{t('router.ledger_col_score')}</th>
                <th>{t('router.ledger_col_status')}</th>
              </tr>
            </thead>
            <tbody id="routeRows">
              {visibleRows.length === 0 ? (
                <tr>
                  <td colSpan={9} className="empty-state">
                    {t('router.ledger_no_routes')}
                  </td>
                </tr>
              ) : (
                visibleRows.map((row) => {
                  const accountName = accounts.find((account) => account.id === row.accountId)?.name ?? row.accountId;
                  const requestLabel = row.path ? `${row.method ?? 'POST'} ${row.path}` : row.id;
                  const metric = metrics.get(row.accountId);
                  const persistedScore =
                    typeof row.routingScore === 'number' && Number.isFinite(row.routingScore) ? row.routingScore : null;
                  const scoreText =
                    persistedScore !== null ? persistedScore.toFixed(3) : metric && Number.isFinite(metric.score) ? metric.score.toFixed(3) : '∞';
                  return (
                    <tr
                      key={row.id}
                      tabIndex={0}
                      className={`route-row${row.accountId === selectedAccountId ? ' match' : ''}${row.id === selectedRouteId ? ' active' : ''}`}
                      onClick={() => onSelectRoute(row)}
                      onKeyDown={(event) => {
                        if (event.key === 'Enter' || event.key === ' ') {
                          event.preventDefault();
                          onSelectRoute(row);
                        }
                      }}
                    >
                      <td>{row.time}</td>
                      <td className="mono">{requestLabel}</td>
                      <td>{row.protocol}</td>
                      <td className="model-cell">{row.model}</td>
                      <td className="account-cell">{accountName}</td>
                      <td>{formatNumber(row.tokens)}</td>
                      <td>{row.latency} ms</td>
                      <td>{scoreText}</td>
                      <td>
                        <span className={`status-badge ${statusClass(row.status)}`}>{t(`common.status_${row.status}`)}</span>
                      </td>
                    </tr>
                  );
                })
              )}
            </tbody>
          </table>
        </div>
      </div>
    </section>
  );
}
