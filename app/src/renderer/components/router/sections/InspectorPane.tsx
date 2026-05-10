import { useTranslation } from 'react-i18next';
import type { Account, RouteMetrics, RouteRow, RoutingMode } from '../../../shared/types';

interface InspectorPaneProps {
  inspectorOpen: boolean;
  selectedRoute: RouteRow;
  selectedRouteAccount: Account;
  selectedMetric: RouteMetrics | undefined;
  routingModes: RoutingMode[];
  formatNumber: (value: number) => string;
  onCloseInspector: () => void;
}

export function InspectorPane({
  inspectorOpen,
  selectedRoute,
  selectedRouteAccount,
  selectedMetric,
  routingModes,
  formatNumber,
  onCloseInspector,
}: InspectorPaneProps) {
  const { t } = useTranslation();
  const strategyId =
    typeof selectedRoute.routingStrategy === 'string' && selectedRoute.routingStrategy.trim() !== ''
      ? selectedRoute.routingStrategy.trim()
      : null;
  const selectedScore =
    typeof selectedRoute.routingScore === 'number' && Number.isFinite(selectedRoute.routingScore)
      ? selectedRoute.routingScore
      : selectedMetric && Number.isFinite(selectedMetric.score)
        ? selectedMetric.score
        : null;
  const strategyMode = strategyId ? routingModes.find((mode) => mode.id === strategyId) : undefined;
  const strategyLabel = strategyMode?.label ?? strategyId ?? 'unknown';
  const strategyFormula = strategyMode?.formula ?? 'not captured';

  return (
    <aside className="pane detail-pane" id="detailPane">
      <header className="section-header">
        <div>
          <p className="eyebrow">{t('router.inspector_eyebrow')}</p>
          <h2>{t('router.inspector_title')}</h2>
        </div>
        <button className="dialog-close" id="closeInspector" type="button" aria-label={t('router.inspector_close')} onClick={onCloseInspector}>
          &times;
        </button>
      </header>
      <div className="pane-body">
        {!inspectorOpen ? (
          <div className="empty-state">{t('router.inspector_select_route')}</div>
        ) : (
          <div className="detail-stack" id="inspector">
            <section className="detail-section">
              <div className="detail-heading">
                <strong>{t('router.inspector_selected_route')}</strong>
                <span>{selectedRoute.protocol}</span>
              </div>
              <div className="key-grid">
                <div className="key-row"><span className="key-label">{t('router.inspector_request')}</span><span className="key-value mono">{selectedRoute.id}</span></div>
                <div className="key-row"><span className="key-label">{t('router.inspector_session')}</span><span className="key-value mono">{selectedRoute.sessionId}</span></div>
                <div className="key-row"><span className="key-label">{t('router.inspector_model')}</span><span className="key-value">{selectedRoute.model}</span></div>
                <div className="key-row"><span className="key-label">{t('router.inspector_account')}</span><span className="key-value">{selectedRouteAccount.name}</span></div>
                <div className="key-row"><span className="key-label">{t('router.inspector_latency')}</span><span className="key-value">{selectedRoute.latency} ms</span></div>
                <div className="key-row"><span className="key-label">{t('router.inspector_score')}</span><span className="key-value">{selectedScore !== null ? selectedScore.toFixed(3) : '∞'}</span></div>
                <div className="key-row"><span className="key-label">{t('router.inspector_affinity')}</span><span className="key-value">{selectedRoute.sticky ? t('router.inspector_sticky_reuse') : t('router.inspector_new_allocation')}</span></div>
                <div className="key-row"><span className="key-label">{t('router.inspector_result')}</span><span className="key-value">{selectedRoute.status === 'ok' ? t('router.inspector_served') : selectedRoute.status === 'warn' ? t('router.inspector_fallback_applied') : t('router.inspector_needs_inspection')}</span></div>
              </div>
            </section>

            <section className="detail-section">
              <div className="detail-heading">
                <strong>{t('router.inspector_account_headroom')}</strong>
                <span>{selectedRouteAccount.note}</span>
              </div>
              <div className="summary-grid">
                <div><span>{t('router.inspector_plan')}</span><strong>{selectedRouteAccount.plan}</strong></div>
                <div>
                  <span>{t('router.inspector_requests_24h')}</span>
                  <strong>{selectedRouteAccount.telemetryBacked ? formatNumber(selectedRouteAccount.routed24h) : '—'}</strong>
                </div>
                <div><span>{t('router.inspector_inflight')}</span><strong>{selectedRouteAccount.telemetryBacked ? selectedRouteAccount.inflight : '—'}</strong></div>
                <div><span>{t('router.inspector_sticky_hit')}</span><strong>{selectedRouteAccount.telemetryBacked ? `${selectedRouteAccount.stickyHit}%` : '—'}</strong></div>
              </div>
            </section>

            <section className="detail-section">
              <div className="detail-heading">
                <strong>{t('router.inspector_dispatch_strategy')}</strong>
                <span>{strategyLabel}</span>
              </div>
              <div className="key-grid">
                <div className="key-row"><span className="key-label">{t('router.inspector_strategy_id')}</span><span className="key-value mono">{strategyId ?? 'unknown'}</span></div>
                <div className="key-row"><span className="key-label">{t('router.inspector_queue_pressure')}</span><span className="key-value">{selectedMetric?.qNorm.toFixed(2) ?? '-'}</span></div>
                <div className="key-row"><span className="key-label">{t('router.inspector_latency_pressure')}</span><span className="key-value">{selectedMetric?.lNorm.toFixed(2) ?? '-'}</span></div>
                <div className="key-row"><span className="key-label">{t('router.inspector_headroom')}</span><span className="key-value">{selectedMetric?.h.toFixed(2) ?? '-'}</span></div>
                <div className="key-row"><span className="key-label">{t('router.inspector_cooldown_flag')}</span><span className="key-value">{selectedMetric?.c ?? '-'}</span></div>
              </div>
            </section>
          </div>
        )}
      </div>
    </aside>
  );
}
