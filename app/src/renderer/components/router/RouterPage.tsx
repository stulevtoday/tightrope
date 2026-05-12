import { useMemo } from 'react';
import {
  useAccountsContext,
  useLogsContext,
  useNavigationContext,
  useRouterDerivedContext,
  useRuntimeContext,
  useSessionsContext,
  useSettingsContext,
} from '../../state/context';
import { InspectorPane } from './sections/InspectorPane';
import { RequestsLedgerPane } from './sections/RequestsLedgerPane';
import { RouterPoolPane } from './sections/RouterPoolPane';
import { RuntimeEventsPanel } from './sections/RuntimeEventsPanel';

export function RouterPage() {
  const navigation = useNavigationContext();
  const accounts = useAccountsContext();
  const logs = useLogsContext();
  const derived = useRouterDerivedContext();
  const runtime = useRuntimeContext();
  const sessions = useSessionsContext();
  const settings = useSettingsContext();
  const recentRouteActivityByAccount = useMemo(() => {
    const latestByAccount = new Map<string, number>();
    for (const row of logs.rows) {
      const requestedAt = typeof row.requestedAt === 'string' ? row.requestedAt.trim() : '';
      if (!requestedAt || !row.accountId) {
        continue;
      }
      const normalized = requestedAt.includes('T') ? requestedAt : `${requestedAt.replace(' ', 'T')}Z`;
      const parsed = new Date(normalized).getTime();
      if (!Number.isFinite(parsed)) {
        continue;
      }
      const current = latestByAccount.get(row.accountId) ?? 0;
      if (parsed > current) {
        latestByAccount.set(row.accountId, parsed);
      }
    }
    return latestByAccount;
  }, [logs.rows]);

  if (navigation.currentPage !== 'router') return null;

  return (
    <section className="workbench page active" id="pageRouter" data-page="router">
      <div className={`route-stage${derived.inspectorOpen ? ' inspector-open' : ''}`}>
        <RouterPoolPane
          accounts={accounts.accounts}
          metrics={derived.metrics}
          routedAccountId={derived.routedAccountId}
          lockedRoutingAccountIds={settings.dashboardSettings.lockedRoutingAccountIds}
          sessions={sessions.sessions}
          clusterStatus={settings.clusterStatus}
          recentRouteActivityByAccount={recentRouteActivityByAccount}
          trafficNowMs={accounts.trafficClockMs}
          trafficActiveWindowMs={accounts.trafficActiveWindowMs}
          selectedAccountId={derived.selectedAccountId}
          onSelectAccount={derived.setSelectedAccountId}
          onTogglePin={accounts.toggleAccountPin}
          onUpdateLockedRoutingAccountIds={settings.updateLockedRoutingAccountIds}
          onOpenSyncTopology={settings.openSyncTopology}
          onOpenAddAccount={accounts.openAddAccountDialog}
        />
        <RequestsLedgerPane
          accounts={accounts.accounts}
          metrics={derived.metrics}
          visibleRows={derived.visibleRows}
          selectedAccountId={derived.selectedAccountId}
          selectedRouteId={derived.selectedRouteId}
          kpis={derived.kpis}
          formatNumber={accounts.formatNumber}
          onSelectRoute={derived.setSelectedRoute}
        />
        <InspectorPane
          inspectorOpen={derived.inspectorOpen}
          selectedRoute={derived.selectedRoute}
          selectedRouteAccount={derived.selectedRouteAccount}
          selectedMetric={derived.selectedMetric}
          routingModes={settings.routingModes}
          formatNumber={accounts.formatNumber}
          onCloseInspector={() => derived.setInspectorOpen(false)}
        />
      </div>
      <RuntimeEventsPanel runtimeState={runtime.runtimeState} />
    </section>
  );
}
