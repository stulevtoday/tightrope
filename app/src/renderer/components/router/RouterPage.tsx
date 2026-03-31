import type { Account, RouteMetrics, RouteRow, RoutingMode, RuntimeState, ScoringModel } from '../../shared/types';
import { InspectorPane } from './sections/InspectorPane';
import { RequestsLedgerPane } from './sections/RequestsLedgerPane';
import { RouterPoolPane } from './sections/RouterPoolPane';
import { RuntimeEventsPanel } from './sections/RuntimeEventsPanel';

interface RouterPageProps {
  visible: boolean;
  accounts: Account[];
  metrics: Map<string, RouteMetrics>;
  routedAccountId: string | null;
  trafficNowMs: number;
  trafficActiveWindowMs: number;
  selectedAccountId: string;
  selectedRouteId: string;
  selectedRoute: RouteRow;
  selectedRouteAccount: Account;
  selectedMetric: RouteMetrics | undefined;
  visibleRows: RouteRow[];
  routingModes: RoutingMode[];
  scoringModel: ScoringModel;
  routingMode: string;
  runtimeState: RuntimeState;
  inspectorOpen: boolean;
  modeLabel: string;
  kpis: { rpm: number; p95: number; failover: number; sticky: number };
  formatNumber: (value: number) => string;
  onSelectAccount: (accountId: string) => void;
  onSelectRoute: (row: RouteRow) => void;
  onCloseInspector: () => void;
  onOpenAddAccount: () => void;
  onSetRoutingMode: (modeId: string) => void;
  onSetScoringWeight: (key: 'alpha' | 'beta' | 'gamma' | 'delta' | 'zeta' | 'eta', value: number) => void;
}

export function RouterPage({
  visible,
  accounts,
  metrics,
  routedAccountId,
  trafficNowMs,
  trafficActiveWindowMs,
  selectedAccountId,
  selectedRouteId,
  selectedRoute,
  selectedRouteAccount,
  selectedMetric,
  visibleRows,
  routingModes,
  scoringModel,
  routingMode,
  runtimeState,
  inspectorOpen,
  modeLabel,
  kpis,
  formatNumber,
  onSelectAccount,
  onSelectRoute,
  onCloseInspector,
  onOpenAddAccount,
  onSetRoutingMode,
  onSetScoringWeight,
}: RouterPageProps) {
  if (!visible) return null;

  return (
    <section className="workbench page active" id="pageRouter" data-page="router">
      <div className={`route-stage${inspectorOpen ? ' inspector-open' : ''}`}>
        <RouterPoolPane
          accounts={accounts}
          metrics={metrics}
          routedAccountId={routedAccountId}
          trafficNowMs={trafficNowMs}
          trafficActiveWindowMs={trafficActiveWindowMs}
          selectedAccountId={selectedAccountId}
          onSelectAccount={onSelectAccount}
          onOpenAddAccount={onOpenAddAccount}
        />
        <RequestsLedgerPane
          accounts={accounts}
          metrics={metrics}
          visibleRows={visibleRows}
          selectedAccountId={selectedAccountId}
          selectedRouteId={selectedRouteId}
          kpis={kpis}
          formatNumber={formatNumber}
          onSelectRoute={onSelectRoute}
        />
        <InspectorPane
          inspectorOpen={inspectorOpen}
          selectedRoute={selectedRoute}
          selectedRouteAccount={selectedRouteAccount}
          selectedMetric={selectedMetric}
          routingModes={routingModes}
          routingMode={routingMode}
          scoringModel={scoringModel}
          modeLabel={modeLabel}
          formatNumber={formatNumber}
          onCloseInspector={onCloseInspector}
          onSetRoutingMode={onSetRoutingMode}
          onSetScoringWeight={onSetScoringWeight}
        />
      </div>
      <RuntimeEventsPanel runtimeState={runtimeState} />
    </section>
  );
}
