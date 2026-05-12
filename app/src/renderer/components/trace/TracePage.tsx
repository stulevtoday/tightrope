import { useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import {
  useAccountsContext,
  useLogsContext,
  useNavigationContext,
  useRouterDerivedContext,
  useSettingsContext,
} from '../../state/context';
import { statusClass } from '../../state/logic';
import type { RouteRow } from '../../shared/types';
import { RoutingTraceMap } from '../router/sections/RoutingTraceMap';

function requestLabel(row: RouteRow): string {
  if (row.path) {
    return `${row.method ?? 'POST'} ${row.path}`;
  }
  return row.id || '—';
}

function valueOrDash(value: string | number | null | undefined): string {
  if (value === null || value === undefined || value === '') {
    return '—';
  }
  return String(value);
}

function scoreValue(value: number | null | undefined): string {
  if (typeof value !== 'number') {
    return '—';
  }
  return Number.isFinite(value) ? value.toFixed(3) : '∞';
}

function fixedValue(value: number | null | undefined, digits: number): string {
  return typeof value === 'number' && Number.isFinite(value) ? value.toFixed(digits) : '—';
}

function costValue(value: number | null | undefined): string {
  if (typeof value !== 'number' || !Number.isFinite(value)) {
    return '—';
  }
  return `$${value.toFixed(value >= 1 ? 2 : 4)}`;
}

export function TracePage() {
  const { t } = useTranslation();
  const navigation = useNavigationContext();
  const accounts = useAccountsContext();
  const logs = useLogsContext();
  const derived = useRouterDerivedContext();
  const settings = useSettingsContext();
  const selectedRoute = useMemo(
    () => logs.rows.find((row) => row.id === derived.selectedRouteId) ?? logs.rows[0] ?? derived.selectedRoute,
    [derived.selectedRoute, derived.selectedRouteId, logs.rows],
  );
  const selectedRouteAccount = useMemo(
    () => accounts.accounts.find((account) => account.id === selectedRoute.accountId) ?? derived.selectedRouteAccount,
    [accounts.accounts, derived.selectedRouteAccount, selectedRoute.accountId],
  );
  const selectedMetric = derived.metrics.get(selectedRoute.accountId);
  const selectedScore = selectedRoute.routingScore ?? selectedMetric?.score;
  const selectedAccountName = selectedRouteAccount.name || selectedRoute.accountId || '—';
  const selectedAccountState = selectedRouteAccount.state ? t(`common.state_${selectedRouteAccount.state}`) : '—';
  const detailRows = {
    request: [
      { label: t('trace.detail_request_id'), value: valueOrDash(selectedRoute.id), mono: true },
      { label: t('trace.detail_route'), value: requestLabel(selectedRoute), mono: true },
      { label: t('router.inspector_model'), value: valueOrDash(selectedRoute.model), mono: true },
      { label: t('trace.detail_protocol'), value: valueOrDash(selectedRoute.protocol), mono: true },
      { label: t('router.inspector_account'), value: selectedAccountName },
      { label: t('trace.detail_account_id'), value: valueOrDash(selectedRoute.accountId), mono: true },
      { label: t('router.inspector_session'), value: valueOrDash(selectedRoute.sessionId), mono: true },
    ],
    decision: [
      { label: t('trace.detail_strategy'), value: valueOrDash(selectedRoute.routingStrategy ?? t('router.trace_strategy_unknown')), mono: true },
      { label: t('trace.detail_routing_score'), value: scoreValue(selectedScore), mono: true },
      { label: t('router.inspector_headroom'), value: fixedValue(selectedMetric?.h, 2), mono: true },
      { label: t('router.inspector_queue_pressure'), value: fixedValue(selectedMetric?.qNorm, 2), mono: true },
      { label: t('router.inspector_latency_pressure'), value: fixedValue(selectedMetric?.lNorm, 2), mono: true },
      { label: t('trace.detail_account_state'), value: selectedAccountState },
      { label: t('trace.detail_capability'), value: selectedMetric ? (selectedMetric.capability ? t('common.status_ok') : t('common.disabled')) : '—' },
      { label: t('router.inspector_affinity'), value: selectedRoute.sticky ? t('router.trace_sticky_hit') : t('router.trace_sticky_new') },
    ],
    packet: [
      { label: t('trace.detail_status'), value: selectedRoute.status ? t(`common.status_${selectedRoute.status}`) : '—' },
      { label: t('drawer.col_status_code'), value: valueOrDash(selectedRoute.statusCode), mono: true },
      { label: t('drawer.col_error_code'), value: valueOrDash(selectedRoute.errorCode), mono: true },
      { label: t('router.ledger_col_tokens'), value: selectedRoute.tokens > 0 ? accounts.formatNumber(selectedRoute.tokens) : '—', mono: true },
      { label: t('router.ledger_col_latency'), value: selectedRoute.latency > 0 ? `${selectedRoute.latency} ${t('common.ms_unit')}` : '—', mono: true },
      { label: t('drawer.col_requested_at'), value: valueOrDash(selectedRoute.requestedAt ?? selectedRoute.time), mono: true },
      { label: t('trace.detail_total_cost'), value: costValue(selectedRoute.totalCost), mono: true },
    ],
  };

  if (navigation.currentPage !== 'trace') return null;

  return (
    <section className="trace-page page active" id="pageTrace" data-page="trace">
      <div className="trace-page-layout">
        <section className="trace-page-primary">
          <header className="section-header trace-page-header">
            <div>
              <p className="eyebrow">{t('trace.eyebrow')}</p>
              <h2>{t('trace.title')}</h2>
            </div>
            <div className="trace-page-summary">
              <span>{t('trace.requests_count', { count: logs.rows.length })}</span>
              <span>{t('trace.accounts_count', { count: accounts.accounts.length })}</span>
            </div>
          </header>

          <div className="trace-page-body">
            <RoutingTraceMap
              accounts={accounts.accounts}
              metrics={derived.metrics}
              visibleRows={logs.rows}
              selectedRoute={selectedRoute}
              selectedRouteAccount={selectedRouteAccount}
              lockedRoutingAccountIds={settings.dashboardSettings.lockedRoutingAccountIds}
              strictLockPoolContinuations={settings.dashboardSettings.strictLockPoolContinuations}
              formatNumber={accounts.formatNumber}
            />

            <section className="trace-requests-pane">
              <header className="trace-requests-header">
                <div>
                  <p className="eyebrow">{t('trace.timeline_eyebrow')}</p>
                  <h3>{t('trace.timeline_title')}</h3>
                </div>
              </header>
              <div className="trace-request-list">
                {logs.rows.length === 0 ? (
                  <div className="empty-state">{t('trace.no_requests')}</div>
                ) : (
                  logs.rows.slice(0, 80).map((row) => {
                    const accountName = accounts.accounts.find((account) => account.id === row.accountId)?.name ?? row.accountId;
                    return (
                      <button
                        key={row.id}
                        className={`trace-request-row${row.id === selectedRoute.id ? ' active' : ''}`}
                        type="button"
                        onClick={() => derived.setSelectedRoute(row)}
                      >
                        <span className="trace-request-time">{row.time}</span>
                        <span className="trace-request-main">
                          <strong>{requestLabel(row)}</strong>
                          <span>{accountName || t('drawer.unassigned')}</span>
                        </span>
                        <span className="trace-request-protocol">{row.protocol}</span>
                        <span className={`status-badge ${statusClass(row.status)}`}>{t(`common.status_${row.status}`)}</span>
                      </button>
                    );
                  })
                )}
              </div>
            </section>
          </div>
        </section>

        <aside className="trace-page-detail">
          <header className="section-header">
            <div>
              <p className="eyebrow">{t('trace.detail_eyebrow')}</p>
              <h2>{t('trace.detail_title')}</h2>
            </div>
          </header>
          <div className="pane-body">
            {!selectedRoute.id ? (
              <div className="empty-state">{t('trace.detail_empty')}</div>
            ) : (
              <div className="detail-stack">
                <section className="detail-section">
                  <div className="detail-heading">
                    <strong>{t('trace.detail_request')}</strong>
                    <span>{selectedRoute.protocol}</span>
                  </div>
                  <div className="trace-detail-list">
                    {detailRows.request.map((row) => (
                      <div className="trace-detail-row" key={row.label}>
                        <span className="trace-detail-label">{row.label}</span>
                        <strong className={`trace-detail-value${row.mono ? ' mono' : ''}`} title={row.value}>{row.value}</strong>
                      </div>
                    ))}
                  </div>
                </section>

                <section className="detail-section">
                  <div className="detail-heading">
                    <strong>{t('trace.detail_decision')}</strong>
                    <span>{selectedRoute.routingStrategy ?? t('router.trace_strategy_unknown')}</span>
                  </div>
                  <div className="trace-detail-list compact">
                    {detailRows.decision.map((row) => (
                      <div className="trace-detail-row" key={row.label}>
                        <span className="trace-detail-label">{row.label}</span>
                        <strong className={`trace-detail-value${row.mono ? ' mono' : ''}`} title={row.value}>{row.value}</strong>
                      </div>
                    ))}
                  </div>
                </section>

                <section className="detail-section">
                  <div className="detail-heading">
                    <strong>{t('trace.detail_packet')}</strong>
                    <span>{selectedRoute.status}</span>
                  </div>
                  <div className="trace-detail-list">
                    {detailRows.packet.map((row) => (
                      <div className="trace-detail-row" key={row.label}>
                        <span className="trace-detail-label">{row.label}</span>
                        <strong className={`trace-detail-value${row.mono ? ' mono' : ''}`} title={row.value}>{row.value}</strong>
                      </div>
                    ))}
                  </div>
                </section>
              </div>
            )}
          </div>
        </aside>
      </div>
    </section>
  );
}
