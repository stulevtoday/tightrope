import { renderHook } from '@testing-library/react';
import { describe, expect, test, vi } from 'vitest';
import { accountsSeed, createInitialRuntimeState, routingModesSeed } from '../../test/fixtures/seed';
import { defaultDashboardSettings } from '../hooks/useSettings';
import type { TightropeModel } from './modelTypes';
import {
  useRouterDerivedContextValue,
  useSessionsContextValue,
  useSettingsContextValue,
} from './useDerivedContextValues';

describe('useDerivedContextValues', () => {
  test('useRouterDerivedContextValue derives selected route fallback and fallback account', () => {
    const state = createInitialRuntimeState();
    state.rows = [{ ...state.rows[0], id: 'route-1', accountId: 'acc-missing', status: 'ok' }];
    state.selectedRouteId = 'route-missing';
    state.selectedAccountId = '';
    state.searchQuery = '';
    state.routingMode = 'weighted_round_robin';
    state.inspectorOpen = false;

    const setSearchQuery = vi.fn();
    const setSelectedAccountId = vi.fn();
    const setSelectedRoute = vi.fn();
    const setInspectorOpen = vi.fn();

    const model = {
      state,
      accounts: [],
      routingModes: routingModesSeed,
      setSearchQuery,
      setSelectedAccountId,
      setSelectedRoute,
      setInspectorOpen,
    } as unknown as TightropeModel;

    const { result } = renderHook(() => useRouterDerivedContextValue(model));

    expect(result.current.visibleRows).toHaveLength(1);
    expect(result.current.selectedRouteId).toBe('route-1');
    expect(result.current.selectedRoute.id).toBe('route-1');
    expect(result.current.selectedRouteAccount.id).toBe('acc-missing');
    expect(result.current.selectedRouteAccount.name).toBe('acc-missing@unassigned.local');
    expect(result.current.routerState).toBe('running');
    expect(result.current.modeLabel).toBe(
      routingModesSeed.find((mode) => mode.id === 'weighted_round_robin')?.label ?? 'weighted_round_robin',
    );
    expect(result.current.setSearchQuery).toBe(setSearchQuery);
    expect(result.current.setSelectedAccountId).toBe(setSelectedAccountId);
    expect(result.current.setSelectedRoute).toBe(setSelectedRoute);
    expect(result.current.setInspectorOpen).toBe(setInspectorOpen);
  });

  test('useRouterDerivedContextValue prefers the runtime selected routed account', () => {
    const state = createInitialRuntimeState();
    state.currentRoutedAccountId = 'acc-night';

    const model = {
      state,
      accounts: accountsSeed,
      routingModes: routingModesSeed,
      setSearchQuery: vi.fn(),
      setSelectedAccountId: vi.fn(),
      setSelectedRoute: vi.fn(),
      setInspectorOpen: vi.fn(),
    } as unknown as TightropeModel;

    const { result } = renderHook(() => useRouterDerivedContextValue(model));

    expect(result.current.routedAccountId).toBe('acc-night');
  });

  test('useSessionsContextValue paginates filtered sessions and maps pager actions', () => {
    const state = createInitialRuntimeState();
    state.sessions = [
      { key: 'sticky-1', kind: 'sticky_thread', accountId: 'acc-1', updated: '2026-03-27 10:00:00', expiry: null, stale: false },
      { key: 'sticky-2', kind: 'sticky_thread', accountId: 'acc-2', updated: '2026-03-27 10:01:00', expiry: null, stale: true },
      { key: 'cache-1', kind: 'prompt_cache', accountId: 'acc-3', updated: '2026-03-27 10:02:00', expiry: null, stale: false },
    ];
    state.sessionsKindFilter = 'sticky_thread';
    state.sessionsOffset = 0;

    const setSessionsKindFilter = vi.fn();
    const prevSessionsPage = vi.fn();
    const nextSessionsPage = vi.fn();
    const purgeStaleSessions = vi.fn();

    const model = {
      state,
      setSessionsKindFilter,
      prevSessionsPage,
      nextSessionsPage,
      purgeStaleSessions,
    } as unknown as TightropeModel;

    const { result } = renderHook(() => useSessionsContextValue(model));

    expect(result.current.sessionsKindFilter).toBe('sticky_thread');
    expect(result.current.sessionsView.filtered).toHaveLength(2);
    expect(result.current.sessionsView.paged).toHaveLength(2);
    expect(result.current.sessionsView.staleTotal).toBe(1);
    expect(result.current.sessionsPaginationLabel).toBe('1–2 of 2');
    expect(result.current.canPrevSessions).toBe(false);
    expect(result.current.canNextSessions).toBe(false);
    expect(result.current.setSessionsKindFilter).toBe(setSessionsKindFilter);
    expect(result.current.prevSessionsPage).toBe(prevSessionsPage);
    expect(result.current.nextSessionsPage).toBe(nextSessionsPage);
    expect(result.current.purgeStaleSessions).toBe(purgeStaleSessions);
  });

  test('useSettingsContextValue projects scoring overrides and method aliases', () => {
    const state = createInitialRuntimeState();
    const setFirewallDraft = vi.fn();
    const saveSettingsChanges = vi.fn();
    const discardDashboardSettingsChanges = vi.fn();

    const model = {
      state,
      routingModes: routingModesSeed,
      dashboardSettings: {
        ...defaultDashboardSettings,
        theme: 'dark',
        routingStrategy: 'round_robin',
        routingHeadroomWeightPrimary: 0.12,
        routingHeadroomWeightSecondary: 0.88,
      },
      firewallMode: 'allow_all',
      firewallEntries: [],
      firewallDraftIpAddress: '10.0.0.0/8',
      clusterStatus: { enabled: false, peers: [] },
      manualPeerAddress: '',
      hasUnsavedSettingsChanges: true,
      settingsSaveInFlight: false,
      setRoutingMode: vi.fn(),
      setStrategyParam: vi.fn(),
      setScoringWeight: vi.fn(),
      setHeadroomWeight: vi.fn(),
      setUpstreamStreamTransport: vi.fn(),
      setStickyThreadsEnabled: vi.fn(),
      setPreferEarlierResetAccounts: vi.fn(),
      setStrictLockPoolContinuations: vi.fn(),
      setOpenaiCacheAffinityMaxAgeSeconds: vi.fn(),
      setImportWithoutOverwrite: vi.fn(),
      setRoutingPlanModelPricingUsdPerMillion: vi.fn(),
      setFirewallDraft,
      addFirewallIpAddress: vi.fn(),
      removeFirewallIpAddress: vi.fn(),
      toggleSyncEnabled: vi.fn(),
      setSyncSiteId: vi.fn(),
      setSyncPort: vi.fn(),
      setSyncDiscoveryEnabled: vi.fn(),
      setSyncClusterName: vi.fn(),
      setManualPeer: vi.fn(),
      addManualPeer: vi.fn(),
      removeSyncPeer: vi.fn(),
      setSyncIntervalSeconds: vi.fn(),
      setSyncConflictResolution: vi.fn(),
      setSyncJournalRetentionDays: vi.fn(),
      setSyncTlsEnabled: vi.fn(),
      setSyncRequireHandshakeAuth: vi.fn(),
      setSyncClusterSharedSecret: vi.fn(),
      setSyncTlsVerifyPeer: vi.fn(),
      setSyncTlsCaCertificatePath: vi.fn(),
      setSyncTlsCertificateChainPath: vi.fn(),
      setSyncTlsPrivateKeyPath: vi.fn(),
      setSyncTlsPinnedPeerCertificateSha256: vi.fn(),
      setSyncSchemaVersion: vi.fn(),
      setSyncMinSupportedSchemaVersion: vi.fn(),
      setSyncAllowSchemaDowngrade: vi.fn(),
      setSyncPeerProbeEnabled: vi.fn(),
      setSyncPeerProbeIntervalMs: vi.fn(),
      setSyncPeerProbeTimeoutMs: vi.fn(),
      setSyncPeerProbeMaxPerRefresh: vi.fn(),
      setSyncPeerProbeFailClosed: vi.fn(),
      setSyncPeerProbeFailClosedFailures: vi.fn(),
      triggerSyncNow: vi.fn(),
      openSyncTopologyDialog: vi.fn(),
      closeSyncTopologyDialog: vi.fn(),
      setTheme: vi.fn(),
      saveSettingsChanges,
      discardDashboardSettingsChanges,
    } as unknown as TightropeModel;

    const { result } = renderHook(() => useSettingsContextValue(model));

    expect(result.current.routingModes).toBe(routingModesSeed);
    expect(result.current.routingMode).toBe('round_robin');
    expect(result.current.theme).toBe('dark');
    expect(result.current.scoringModel.wp).toBe(0.12);
    expect(result.current.scoringModel.ws).toBe(0.88);
    expect(result.current.settingsDirty).toBe(true);
    expect(result.current.settingsSaving).toBe(false);
    expect(result.current.setFirewallDraftIpAddress).toBe(setFirewallDraft);
    expect(result.current.saveSettings).toBe(saveSettingsChanges);
    expect(result.current.discardSettings).toBe(discardDashboardSettingsChanges);
  });
});
