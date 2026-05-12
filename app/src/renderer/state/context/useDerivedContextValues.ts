import { useMemo } from 'react';
import type { Account, RouteRow } from '../../shared/types';
import { SESSIONS_PAGE_SIZE } from '../config';
import type { RouterDerivedContextValue } from './RouterDerivedContext';
import type { SessionsContextValue } from './SessionsContext';
import type { SettingsContextValue } from './SettingsContext';
import type { TightropeModel } from './modelTypes';
import {
  currentRouterState,
  deriveKpis,
  deriveMetrics,
  ensureSelectedRouteId,
  filteredRows,
  modeLabel,
  paginateSessions,
  selectRoutedAccountId,
} from '../logic';

const EMPTY_ROUTE_ROW: RouteRow = {
  time: '--:--:--',
  id: '',
  model: '—',
  accountId: '',
  tokens: 0,
  latency: 0,
  status: 'ok',
  protocol: 'SSE',
  sessionId: '',
  sticky: false,
};

function fallbackRouteAccount(accountId: string): Account {
  return {
    id: accountId,
    name: `${accountId}@unassigned.local`,
    pinned: false,
    plan: 'free',
    health: 'strained',
    state: 'deactivated',
    inflight: 0,
    load: 0,
    latency: 0,
    errorEwma: 0,
    cooldown: true,
    capability: false,
    costNorm: 0,
    routed24h: 0,
    stickyHit: 0,
    quotaPrimary: 0,
    quotaSecondary: 0,
    failovers: 0,
    note: 'openai',
    telemetryBacked: false,
    trafficUpBytes: 0,
    trafficDownBytes: 0,
    trafficLastUpAtMs: 0,
    trafficLastDownAtMs: 0,
  };
}

export function useRouterDerivedContextValue(model: TightropeModel): RouterDerivedContextValue {
  const metrics = useMemo(
    () => deriveMetrics(model.accounts, model.state.scoringModel, model.state.routingMode, model.state.roundRobinCursor, model.routingModes),
    [model.accounts, model.state.roundRobinCursor, model.state.routingMode, model.state.scoringModel, model.routingModes],
  );
  const visibleRows = useMemo(
    () => filteredRows(model.state.rows, model.accounts, model.state.selectedAccountId, model.state.searchQuery),
    [model.accounts, model.state.rows, model.state.searchQuery, model.state.selectedAccountId],
  );
  const selectedRouteId = useMemo(
    () => ensureSelectedRouteId(visibleRows, model.state.selectedRouteId),
    [model.state.selectedRouteId, visibleRows],
  );
  const selectedRoute = useMemo(
    () => model.state.rows.find((route) => route.id === selectedRouteId) ?? model.state.rows[0] ?? EMPTY_ROUTE_ROW,
    [model.state.rows, selectedRouteId],
  );
  const selectedRouteAccount = useMemo(
    () => model.accounts.find((account) => account.id === selectedRoute.accountId) ?? fallbackRouteAccount(selectedRoute.accountId),
    [model.accounts, selectedRoute.accountId],
  );
  const selectedMetric = useMemo(
    () => metrics.get(selectedRouteAccount.id),
    [metrics, selectedRouteAccount.id],
  );
  const kpis = useMemo(
    () => deriveKpis(model.state.rows, model.accounts),
    [model.accounts, model.state.rows],
  );
  const fallbackRoutedAccountId = useMemo(
    () => selectRoutedAccountId(model.accounts, metrics, model.state.rows),
    [metrics, model.accounts, model.state.rows],
  );
  const routedAccountId = useMemo(() => {
    const signaledAccountId = model.state.currentRoutedAccountId?.trim() ?? '';
    if (signaledAccountId && model.accounts.some((account) => account.id === signaledAccountId)) {
      return signaledAccountId;
    }
    return fallbackRoutedAccountId;
  }, [fallbackRoutedAccountId, model.accounts, model.state.currentRoutedAccountId]);

  const routerState = useMemo(
    () => currentRouterState(model.state.runtimeState),
    [model.state.runtimeState],
  );
  const routingModeLabel = useMemo(
    () => modeLabel(model.state.routingMode, model.routingModes),
    [model.routingModes, model.state.routingMode],
  );

  return useMemo(
    () => ({
      metrics,
      visibleRows,
      searchQuery: model.state.searchQuery,
      selectedAccountId: model.state.selectedAccountId,
      selectedRouteId,
      inspectorOpen: model.state.inspectorOpen,
      routedAccountId,
      selectedRoute,
      selectedRouteAccount,
      selectedMetric,
      kpis,
      routerState,
      modeLabel: routingModeLabel,
      setSearchQuery: model.setSearchQuery,
      setSelectedAccountId: model.setSelectedAccountId,
      setSelectedRoute: model.setSelectedRoute,
      setInspectorOpen: model.setInspectorOpen,
    }),
    [
      kpis,
      metrics,
      model.setInspectorOpen,
      model.setSearchQuery,
      model.setSelectedAccountId,
      model.setSelectedRoute,
      model.state.inspectorOpen,
      model.state.searchQuery,
      model.state.selectedAccountId,
      routedAccountId,
      routerState,
      routingModeLabel,
      selectedMetric,
      selectedRoute,
      selectedRouteAccount,
      selectedRouteId,
      visibleRows,
    ],
  );
}

export function useSessionsContextValue(model: TightropeModel): SessionsContextValue {
  const sessionsView = useMemo(
    () => paginateSessions(model.state.sessions, model.state.sessionsKindFilter, model.state.sessionsOffset, SESSIONS_PAGE_SIZE),
    [model.state.sessions, model.state.sessionsKindFilter, model.state.sessionsOffset],
  );
  const sessionsStart = Math.min(model.state.sessionsOffset + 1, sessionsView.filtered.length);
  const sessionsEnd = Math.min(model.state.sessionsOffset + SESSIONS_PAGE_SIZE, sessionsView.filtered.length);

  const sessionsPaginationLabel = useMemo(
    () => (sessionsView.filtered.length > 0 ? `${sessionsStart}–${sessionsEnd} of ${sessionsView.filtered.length}` : '0 results'),
    [sessionsEnd, sessionsStart, sessionsView.filtered.length],
  );
  const canPrevSessions = model.state.sessionsOffset > 0;
  const canNextSessions = model.state.sessionsOffset + SESSIONS_PAGE_SIZE < sessionsView.filtered.length;

  return useMemo(
    () => ({
      sessions: model.state.sessions,
      sessionsKindFilter: model.state.sessionsKindFilter,
      sessionsView,
      sessionsPaginationLabel,
      canPrevSessions,
      canNextSessions,
      setSessionsKindFilter: model.setSessionsKindFilter,
      prevSessionsPage: model.prevSessionsPage,
      nextSessionsPage: model.nextSessionsPage,
      purgeStaleSessions: model.purgeStaleSessions,
    }),
    [
      canNextSessions,
      canPrevSessions,
      model.nextSessionsPage,
      model.prevSessionsPage,
      model.purgeStaleSessions,
      model.setSessionsKindFilter,
      model.state.sessions,
      model.state.sessionsKindFilter,
      sessionsPaginationLabel,
      sessionsView,
    ],
  );
}

export function useSettingsContextValue(model: TightropeModel): SettingsContextValue {
  const settingsScoringModel = useMemo(
    () => ({
      ...model.state.scoringModel,
      wp: model.dashboardSettings.routingHeadroomWeightPrimary,
      ws: model.dashboardSettings.routingHeadroomWeightSecondary,
      alpha: model.dashboardSettings.routingScoreAlpha,
      beta: model.dashboardSettings.routingScoreBeta,
      gamma: model.dashboardSettings.routingScoreGamma,
      delta: model.dashboardSettings.routingScoreDelta,
      zeta: model.dashboardSettings.routingScoreZeta,
      eta: model.dashboardSettings.routingScoreEta,
    }),
    [model.dashboardSettings, model.state.scoringModel],
  );

  return useMemo(
    () => ({
      routingModes: model.routingModes,
      routingMode: model.dashboardSettings.routingStrategy,
      scoringModel: settingsScoringModel,
      theme: model.dashboardSettings.theme,
      dashboardSettings: model.dashboardSettings,
      firewallMode: model.firewallMode,
      firewallEntries: model.firewallEntries,
      firewallDraftIpAddress: model.firewallDraftIpAddress,
      clusterStatus: model.clusterStatus,
      manualPeerAddress: model.manualPeerAddress,
      syncTopologyDialogOpen: model.state.syncTopologyDialogOpen,
      settingsDirty: model.hasUnsavedSettingsChanges,
      settingsSaving: model.settingsSaveInFlight,
      setRoutingMode: model.setRoutingMode,
      setStrategyParam: model.setStrategyParam,
      setScoringWeight: model.setScoringWeight,
      setHeadroomWeight: model.setHeadroomWeight,
      setUpstreamStreamTransport: model.setUpstreamStreamTransport,
      setStickyThreadsEnabled: model.setStickyThreadsEnabled,
      setPreferEarlierResetAccounts: model.setPreferEarlierResetAccounts,
      setStrictLockPoolContinuations: model.setStrictLockPoolContinuations,
      updateLockedRoutingAccountIds: model.updateLockedRoutingAccountIds,
      setOpenaiCacheAffinityMaxAgeSeconds: model.setOpenaiCacheAffinityMaxAgeSeconds,
      setImportWithoutOverwrite: model.setImportWithoutOverwrite,
      setRoutingPlanModelPricingUsdPerMillion: model.setRoutingPlanModelPricingUsdPerMillion,
      setFirewallDraftIpAddress: model.setFirewallDraft,
      addFirewallIpAddress: model.addFirewallIpAddress,
      removeFirewallIpAddress: model.removeFirewallIpAddress,
      toggleSyncEnabled: model.toggleSyncEnabled,
      setSyncSiteId: model.setSyncSiteId,
      setSyncPort: model.setSyncPort,
      setSyncDiscoveryEnabled: model.setSyncDiscoveryEnabled,
      setSyncClusterName: model.setSyncClusterName,
      setManualPeerAddress: model.setManualPeer,
      addManualPeer: model.addManualPeer,
      removePeer: model.removeSyncPeer,
      setSyncIntervalSeconds: model.setSyncIntervalSeconds,
      setSyncConflictResolution: model.setSyncConflictResolution,
      setSyncJournalRetentionDays: model.setSyncJournalRetentionDays,
      setSyncTlsEnabled: model.setSyncTlsEnabled,
      setSyncRequireHandshakeAuth: model.setSyncRequireHandshakeAuth,
      setSyncClusterSharedSecret: model.setSyncClusterSharedSecret,
      setSyncTlsVerifyPeer: model.setSyncTlsVerifyPeer,
      setSyncTlsCaCertificatePath: model.setSyncTlsCaCertificatePath,
      setSyncTlsCertificateChainPath: model.setSyncTlsCertificateChainPath,
      setSyncTlsPrivateKeyPath: model.setSyncTlsPrivateKeyPath,
      setSyncTlsPinnedPeerCertificateSha256: model.setSyncTlsPinnedPeerCertificateSha256,
      setSyncSchemaVersion: model.setSyncSchemaVersion,
      setSyncMinSupportedSchemaVersion: model.setSyncMinSupportedSchemaVersion,
      setSyncAllowSchemaDowngrade: model.setSyncAllowSchemaDowngrade,
      setSyncPeerProbeEnabled: model.setSyncPeerProbeEnabled,
      setSyncPeerProbeIntervalMs: model.setSyncPeerProbeIntervalMs,
      setSyncPeerProbeTimeoutMs: model.setSyncPeerProbeTimeoutMs,
      setSyncPeerProbeMaxPerRefresh: model.setSyncPeerProbeMaxPerRefresh,
      setSyncPeerProbeFailClosed: model.setSyncPeerProbeFailClosed,
      setSyncPeerProbeFailClosedFailures: model.setSyncPeerProbeFailClosedFailures,
      triggerSyncNow: model.triggerSyncNow,
      openSyncTopology: model.openSyncTopologyDialog,
      closeSyncTopology: model.closeSyncTopologyDialog,
      setTheme: model.setTheme,
      saveSettings: model.saveSettingsChanges,
      discardSettings: model.discardDashboardSettingsChanges,
    }),
    [
      model.addFirewallIpAddress,
      model.addManualPeer,
      model.clusterStatus,
      model.closeSyncTopologyDialog,
      model.dashboardSettings,
      model.discardDashboardSettingsChanges,
      model.firewallDraftIpAddress,
      model.firewallEntries,
      model.firewallMode,
      model.hasUnsavedSettingsChanges,
      model.manualPeerAddress,
      model.openSyncTopologyDialog,
      model.removeFirewallIpAddress,
      model.removeSyncPeer,
      model.routingModes,
      model.saveSettingsChanges,
      model.setFirewallDraft,
      model.setHeadroomWeight,
      model.setImportWithoutOverwrite,
      model.setManualPeer,
      model.setOpenaiCacheAffinityMaxAgeSeconds,
      model.setPreferEarlierResetAccounts,
      model.setRoutingMode,
      model.setRoutingPlanModelPricingUsdPerMillion,
      model.setScoringWeight,
      model.setStrictLockPoolContinuations,
      model.setStickyThreadsEnabled,
      model.setStrategyParam,
      model.updateLockedRoutingAccountIds,
      model.setSyncAllowSchemaDowngrade,
      model.setSyncClusterName,
      model.setSyncClusterSharedSecret,
      model.setSyncConflictResolution,
      model.setSyncDiscoveryEnabled,
      model.setSyncIntervalSeconds,
      model.setSyncJournalRetentionDays,
      model.setSyncMinSupportedSchemaVersion,
      model.setSyncPeerProbeEnabled,
      model.setSyncPeerProbeFailClosed,
      model.setSyncPeerProbeFailClosedFailures,
      model.setSyncPeerProbeIntervalMs,
      model.setSyncPeerProbeMaxPerRefresh,
      model.setSyncPeerProbeTimeoutMs,
      model.setSyncPort,
      model.setSyncRequireHandshakeAuth,
      model.setSyncSchemaVersion,
      model.setSyncSiteId,
      model.setSyncTlsCaCertificatePath,
      model.setSyncTlsCertificateChainPath,
      model.setSyncTlsEnabled,
      model.setSyncTlsPinnedPeerCertificateSha256,
      model.setSyncTlsPrivateKeyPath,
      model.setSyncTlsVerifyPeer,
      model.setTheme,
      model.setUpstreamStreamTransport,
      model.settingsSaveInFlight,
      model.state.syncTopologyDialogOpen,
      model.toggleSyncEnabled,
      model.triggerSyncNow,
      settingsScoringModel,
    ],
  );
}
