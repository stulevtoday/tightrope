import type { Account, RouteMetrics, RouteRow, RoutingMode, ScoringModel } from '../../../shared/types';
import { sliderTrackStyle } from '../../../shared/styles';

interface InspectorPaneProps {
  inspectorOpen: boolean;
  selectedRoute: RouteRow;
  selectedRouteAccount: Account;
  selectedMetric: RouteMetrics | undefined;
  routingModes: RoutingMode[];
  routingMode: string;
  scoringModel: ScoringModel;
  modeLabel: string;
  formatNumber: (value: number) => string;
  onCloseInspector: () => void;
  onSetRoutingMode: (modeId: string) => void;
  onSetScoringWeight: (key: 'alpha' | 'beta' | 'gamma' | 'delta' | 'zeta' | 'eta', value: number) => void;
}

export function InspectorPane({
  inspectorOpen,
  selectedRoute,
  selectedRouteAccount,
  selectedMetric,
  routingModes,
  routingMode,
  scoringModel,
  modeLabel,
  formatNumber,
  onCloseInspector,
  onSetRoutingMode,
  onSetScoringWeight,
}: InspectorPaneProps) {
  return (
    <aside className="pane detail-pane" id="detailPane">
      <header className="section-header">
        <div>
          <p className="eyebrow">Inspector</p>
          <h2>Route details</h2>
        </div>
        <button className="dialog-close" id="closeInspector" type="button" aria-label="Close inspector" onClick={onCloseInspector}>
          &times;
        </button>
      </header>
      <div className="pane-body">
        {!inspectorOpen ? (
          <div className="empty-state">Select a route to inspect routing details.</div>
        ) : (
          <div className="detail-stack" id="inspector">
            <section className="detail-section">
              <div className="detail-heading">
                <strong>Selected route</strong>
                <span>{selectedRoute.protocol}</span>
              </div>
              <div className="key-grid">
                <div className="key-row"><span className="key-label">Request</span><span className="key-value mono">{selectedRoute.id}</span></div>
                <div className="key-row"><span className="key-label">Session</span><span className="key-value mono">{selectedRoute.sessionId}</span></div>
                <div className="key-row"><span className="key-label">Model</span><span className="key-value">{selectedRoute.model}</span></div>
                <div className="key-row"><span className="key-label">Account</span><span className="key-value">{selectedRouteAccount.name}</span></div>
                <div className="key-row"><span className="key-label">Latency</span><span className="key-value">{selectedRoute.latency} ms</span></div>
                <div className="key-row"><span className="key-label">Score</span><span className="key-value">{selectedMetric && Number.isFinite(selectedMetric.score) ? selectedMetric.score.toFixed(3) : '∞'}</span></div>
                <div className="key-row"><span className="key-label">Affinity</span><span className="key-value">{selectedRoute.sticky ? 'sticky reuse' : 'new allocation'}</span></div>
                <div className="key-row"><span className="key-label">Result</span><span className="key-value">{selectedRoute.status === 'ok' ? 'served' : selectedRoute.status === 'warn' ? 'fallback applied' : 'needs inspection'}</span></div>
              </div>
            </section>

            <section className="detail-section">
              <div className="detail-heading">
                <strong>Account headroom</strong>
                <span>{selectedRouteAccount.note}</span>
              </div>
              <div className="summary-grid">
                <div><span>Plan</span><strong>{selectedRouteAccount.plan}</strong></div>
                <div>
                  <span>Requests (24h)</span>
                  <strong>{selectedRouteAccount.telemetryBacked ? formatNumber(selectedRouteAccount.routed24h) : '—'}</strong>
                </div>
                <div><span>Inflight</span><strong>{selectedRouteAccount.telemetryBacked ? selectedRouteAccount.inflight : '—'}</strong></div>
                <div><span>Sticky hit</span><strong>{selectedRouteAccount.telemetryBacked ? `${selectedRouteAccount.stickyHit}%` : '—'}</strong></div>
              </div>
            </section>

            <details className="detail-fold" open>
              <summary>
                <strong>Strategy tuning</strong>
                <span>{modeLabel}</span>
              </summary>
              <div className="fold-content">
                <div className="mode-grid">
                  {routingModes.map((mode) => (
                    <button
                      key={mode.id}
                      type="button"
                      className={`mode-btn${mode.id === routingMode ? ' active' : ''}`}
                      onClick={() => onSetRoutingMode(mode.id)}
                    >
                      {mode.label}
                    </button>
                  ))}
                </div>
                <div className="weight-stack">
                  {(['alpha', 'beta', 'gamma', 'delta', 'zeta', 'eta'] as const).map((key) => (
                    <label key={key} className="weight-row">
                      <span>{key}</span>
                      <input
                        type="range"
                        min={0}
                        max={1}
                        step={0.01}
                        value={scoringModel[key]}
                        style={sliderTrackStyle(scoringModel[key])}
                        onChange={(event) => onSetScoringWeight(key, Number(event.target.value))}
                      />
                      <strong>{scoringModel[key].toFixed(2)}</strong>
                    </label>
                  ))}
                </div>
                <div className="key-grid">
                  <div className="key-row"><span className="key-label">Queue pressure</span><span className="key-value">{selectedMetric?.qNorm.toFixed(2) ?? '-'}</span></div>
                  <div className="key-row"><span className="key-label">Latency pressure</span><span className="key-value">{selectedMetric?.lNorm.toFixed(2) ?? '-'}</span></div>
                  <div className="key-row"><span className="key-label">Headroom</span><span className="key-value">{selectedMetric?.h.toFixed(2) ?? '-'}</span></div>
                  <div className="key-row"><span className="key-label">Cooldown flag</span><span className="key-value">{selectedMetric?.c ?? '-'}</span></div>
                </div>
              </div>
            </details>
          </div>
        )}
      </div>
    </aside>
  );
}
