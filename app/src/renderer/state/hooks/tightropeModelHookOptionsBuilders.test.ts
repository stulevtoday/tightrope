import { describe, expect, test, vi } from 'vitest';
import { createInitialRuntimeState } from '../../test/fixtures/seed';
import type { Account } from '../../shared/types';
import { defaultDashboardSettings, makeSettingsUpdate } from './useSettings';
import {
  buildAccountsDerivedViewOptions,
  buildAccountsOptions,
  buildClusterSyncOptions,
  buildFirewallOptions,
  buildNavigationOptions,
  buildOauthFlowOptions,
  buildRequestLogOptions,
  buildRuntimeStateOptions,
  buildSelectedAccountActionsOptions,
  buildSettingsOptions,
  buildSettingsSaveActionsOptions,
  buildSessionsOptions,
  buildUiStateActionsOptions,
} from './tightropeModelHookOptionsBuilders';

function sortedKeys(value: object): string[] {
  return Object.keys(value).sort();
}

describe('tightropeModelHookOptionsBuilders', () => {
  test('buildSettingsOptions wires settings dependencies', () => {
    const setState = vi.fn();
    const pushRuntimeEvent = vi.fn();
    const service = {
      getSettingsRequest: vi.fn(async () => null),
      updateSettingsRequest: vi.fn(async () => null),
    };

    const options = buildSettingsOptions({
      stateTheme: 'dark',
      stateRoutingMode: 'weighted_round_robin',
      setState,
      pushRuntimeEvent,
      service,
    });

    expect(options.stateTheme).toBe('dark');
    expect(options.stateRoutingMode).toBe('weighted_round_robin');
    expect(options.setState).toBe(setState);
    expect(options.pushRuntimeEvent).toBe(pushRuntimeEvent);
    expect(options.getSettingsRequest).toBe(service.getSettingsRequest);
    expect(options.updateSettingsRequest).toBe(service.updateSettingsRequest);
    expect(sortedKeys(options)).toEqual([
      'getSettingsRequest',
      'pushRuntimeEvent',
      'setState',
      'stateRoutingMode',
      'stateTheme',
      'updateSettingsRequest',
    ]);
  });

  test('buildAccountsOptions wires accounts dependencies', () => {
    const pushRuntimeEvent = vi.fn();
    const service = {
      listAccountsRequest: vi.fn(async () => []),
      listAccountTrafficRequest: vi.fn(async () => []),
      refreshAccountUsageTelemetryRequest: vi.fn(async () => ({
        accountId: 'acc-1',
        email: 'acc-1@test.local',
        provider: 'openai',
        status: 'active',
      })),
      refreshAccountTokenRequest: vi.fn(async () => ({
        accountId: 'acc-1',
        email: 'acc-1@test.local',
        provider: 'openai',
        status: 'active',
      })),
      pinAccountRequest: vi.fn(async () => {}),
      unpinAccountRequest: vi.fn(async () => {}),
      pauseAccountRequest: vi.fn(async () => {}),
      reactivateAccountRequest: vi.fn(async () => {}),
      deleteAccountRequest: vi.fn(async () => {}),
    };

    const options = buildAccountsOptions({
      runtimeBind: '127.0.0.1:2455',
      pushRuntimeEvent,
      service,
    });

    expect(options.runtimeBind).toBe('127.0.0.1:2455');
    expect(options.pushRuntimeEvent).toBe(pushRuntimeEvent);
    expect(options.service).toBe(service);
    expect(sortedKeys(options)).toEqual(['pushRuntimeEvent', 'runtimeBind', 'service']);
  });

  test('buildOauthFlowOptions wires oauth flow dependencies', () => {
    const state = createInitialRuntimeState();
    const accounts: Account[] = [];
    const setState = vi.fn();
    const accountsState = {
      refreshAccountsFromNative: vi.fn(async () => []),
      refreshUsageTelemetryAfterAccountAdd: vi.fn(async () => {}),
    };
    const pushRuntimeEvent = vi.fn();
    const service = {
      oauthStartRequest: vi.fn(async () => ({
        method: 'browser',
        authorizationUrl: null,
        callbackUrl: null,
        verificationUrl: null,
        userCode: null,
        deviceAuthId: null,
        intervalSeconds: null,
        expiresInSeconds: null,
      })),
      oauthStatusRequest: vi.fn(async () => ({ status: 'pending', errorMessage: null })),
      oauthStopRequest: vi.fn(async () => ({ status: 'stopped', errorMessage: null })),
      oauthRestartRequest: vi.fn(async () => ({
        method: 'browser',
        authorizationUrl: null,
        callbackUrl: null,
        verificationUrl: null,
        userCode: null,
        deviceAuthId: null,
        intervalSeconds: null,
        expiresInSeconds: null,
      })),
      oauthCompleteRequest: vi.fn(async () => ({ status: 'pending' })),
      oauthManualCallbackRequest: vi.fn(async () => ({ status: 'success', errorMessage: null })),
      importAccountRequest: vi.fn(async () => ({
        accountId: 'acc-1',
        email: 'acc-1@test.local',
        provider: 'openai',
        status: 'active',
      })),
      onOauthDeepLinkRequest: vi.fn(() => () => {}),
    };

    const options = buildOauthFlowOptions({
      state,
      accounts,
      setState,
      accountsState,
      pushRuntimeEvent,
      service,
    });

    expect(options.state).toBe(state);
    expect(options.accounts).toBe(accounts);
    expect(options.setState).toBe(setState);
    expect(options.refreshAccountsFromNative).toBe(accountsState.refreshAccountsFromNative);
    expect(options.refreshUsageTelemetryAfterAccountAdd).toBe(accountsState.refreshUsageTelemetryAfterAccountAdd);
    expect(options.pushRuntimeEvent).toBe(pushRuntimeEvent);
    expect(options.oauthStartRequest).toBe(service.oauthStartRequest);
    expect(options.oauthStatusRequest).toBe(service.oauthStatusRequest);
    expect(options.oauthStopRequest).toBe(service.oauthStopRequest);
    expect(options.oauthRestartRequest).toBe(service.oauthRestartRequest);
    expect(options.oauthCompleteRequest).toBe(service.oauthCompleteRequest);
    expect(options.oauthManualCallbackRequest).toBe(service.oauthManualCallbackRequest);
    expect(options.importAccountRequest).toBe(service.importAccountRequest);
    expect(options.onOauthDeepLinkRequest).toBe(service.onOauthDeepLinkRequest);
    expect(sortedKeys(options)).toEqual([
      'accounts',
      'importAccountRequest',
      'oauthCompleteRequest',
      'oauthManualCallbackRequest',
      'oauthRestartRequest',
      'oauthStartRequest',
      'oauthStatusRequest',
      'oauthStopRequest',
      'onOauthDeepLinkRequest',
      'pushRuntimeEvent',
      'refreshAccountsFromNative',
      'refreshUsageTelemetryAfterAccountAdd',
      'setState',
      'state',
    ]);
  });

  test('buildFirewallOptions wires firewall dependencies', () => {
    const pushRuntimeEvent = vi.fn();
    const listFirewallIpsRequest = vi.fn(async () => null);
    const addFirewallIpRequest = vi.fn(async () => true);
    const removeFirewallIpRequest = vi.fn(async () => true);
    const options = buildFirewallOptions({
      pushRuntimeEvent,
      listFirewallIpsRequest,
      addFirewallIpRequest,
      removeFirewallIpRequest,
    });

    expect(options.pushRuntimeEvent).toBe(pushRuntimeEvent);
    expect(options.listFirewallIpsRequest).toBe(listFirewallIpsRequest);
    expect(options.addFirewallIpRequest).toBe(addFirewallIpRequest);
    expect(options.removeFirewallIpRequest).toBe(removeFirewallIpRequest);
    expect(sortedKeys(options)).toEqual([
      'addFirewallIpRequest',
      'listFirewallIpsRequest',
      'pushRuntimeEvent',
      'removeFirewallIpRequest',
    ]);
  });

  test('buildRuntimeStateOptions wires runtime dependencies', () => {
    const state = createInitialRuntimeState();
    const setState = vi.fn();
    const pushRuntimeEvent = vi.fn();
    const backendStatusRequest = vi.fn(async () => ({ enabled: true }));
    const backendStartRequest = vi.fn(async () => ({ enabled: true }));
    const backendStopRequest = vi.fn(async () => ({ enabled: false }));

    const options = buildRuntimeStateOptions({
      runtimeState: state.runtimeState,
      setState,
      pushRuntimeEvent,
      backendStatusRequest,
      backendStartRequest,
      backendStopRequest,
    });

    expect(options.runtimeState).toBe(state.runtimeState);
    expect(options.setState).toBe(setState);
    expect(options.pushRuntimeEvent).toBe(pushRuntimeEvent);
    expect(options.backendStatusRequest).toBe(backendStatusRequest);
    expect(options.backendStartRequest).toBe(backendStartRequest);
    expect(options.backendStopRequest).toBe(backendStopRequest);
    expect(sortedKeys(options)).toEqual([
      'backendStartRequest',
      'backendStatusRequest',
      'backendStopRequest',
      'pushRuntimeEvent',
      'runtimeState',
      'setState',
    ]);
  });

  test('buildClusterSyncOptions wires sync dependencies', () => {
    const dashboardSettings = { ...defaultDashboardSettings };
    const appliedDashboardSettings = { ...defaultDashboardSettings };
    const settingsState = {
      applyPersistedDashboardSettings: vi.fn(),
    };
    const pushRuntimeEvent = vi.fn();
    const service = {
      getClusterStatusRequest: vi.fn(async () => null),
      clusterEnableRequest: vi.fn(async () => true),
      clusterDisableRequest: vi.fn(async () => true),
      addPeerRequest: vi.fn(async () => true),
      removePeerRequest: vi.fn(async () => true),
      triggerSyncRequest: vi.fn(async () => true),
      updateSettingsRequest: vi.fn(async () => null),
    };

    const options = buildClusterSyncOptions({
      dashboardSettings,
      appliedDashboardSettings,
      hasUnsavedSettingsChanges: true,
      makeSettingsUpdate,
      settingsState,
      pushRuntimeEvent,
      service,
    });

    expect(options.dashboardSettings).toBe(dashboardSettings);
    expect(options.appliedDashboardSettings).toBe(appliedDashboardSettings);
    expect(options.hasUnsavedSettingsChanges).toBe(true);
    expect(options.makeSettingsUpdate).toBe(makeSettingsUpdate);
    expect(options.applyPersistedDashboardSettings).toBe(settingsState.applyPersistedDashboardSettings);
    expect(options.pushRuntimeEvent).toBe(pushRuntimeEvent);
    expect(options.getClusterStatusRequest).toBe(service.getClusterStatusRequest);
    expect(options.clusterEnableRequest).toBe(service.clusterEnableRequest);
    expect(options.clusterDisableRequest).toBe(service.clusterDisableRequest);
    expect(options.addPeerRequest).toBe(service.addPeerRequest);
    expect(options.removePeerRequest).toBe(service.removePeerRequest);
    expect(options.triggerSyncRequest).toBe(service.triggerSyncRequest);
    expect(options.updateSettingsRequest).toBe(service.updateSettingsRequest);
    expect(sortedKeys(options)).toEqual([
      'addPeerRequest',
      'appliedDashboardSettings',
      'applyPersistedDashboardSettings',
      'clusterDisableRequest',
      'clusterEnableRequest',
      'dashboardSettings',
      'getClusterStatusRequest',
      'hasUnsavedSettingsChanges',
      'makeSettingsUpdate',
      'pushRuntimeEvent',
      'removePeerRequest',
      'triggerSyncRequest',
      'updateSettingsRequest',
    ]);
  });

  test('buildNavigationOptions wires navigation dependencies', () => {
    const setState = vi.fn();
    const settingsActions = {
      discardDashboardSettingsChanges: vi.fn(),
      saveDashboardSettings: vi.fn(async () => true),
    };

    const options = buildNavigationOptions({
      currentPage: 'settings',
      hasUnsavedSettingsChanges: true,
      settingsSaveInFlight: true,
      setState,
      settingsActions,
    });

    expect(options.currentPage).toBe('settings');
    expect(options.hasUnsavedSettingsChanges).toBe(true);
    expect(options.settingsSaveInFlight).toBe(true);
    expect(options.setState).toBe(setState);
    expect(options.discardDashboardSettingsChanges).toBe(settingsActions.discardDashboardSettingsChanges);
    expect(options.saveDashboardSettings).toBe(settingsActions.saveDashboardSettings);
    expect(sortedKeys(options)).toEqual([
      'currentPage',
      'discardDashboardSettingsChanges',
      'hasUnsavedSettingsChanges',
      'saveDashboardSettings',
      'setState',
      'settingsSaveInFlight',
    ]);
  });

  test('buildSettingsSaveActionsOptions wires save action dependencies', () => {
    const settingsState = {
      discardDashboardSettingsChanges: vi.fn(),
      saveDashboardSettings: vi.fn(async () => ({
        saved: true,
        previousApplied: { ...defaultDashboardSettings },
        updated: { ...defaultDashboardSettings },
      })),
    };
    const clusterSyncState = {
      clusterStatus: {
        enabled: true,
      },
      reconfigureSyncCluster: vi.fn(async () => {}),
    };

    const options = buildSettingsSaveActionsOptions({
      settingsState,
      clusterSyncState,
    });

    expect(options.settingsState).toBe(settingsState);
    expect(options.clusterSyncEnabled).toBe(true);
    expect(options.reconfigureSyncCluster).toBe(clusterSyncState.reconfigureSyncCluster);
    expect(sortedKeys(options)).toEqual(['clusterSyncEnabled', 'reconfigureSyncCluster', 'settingsState']);
  });

  test('buildUiStateActionsOptions wires ui state dependencies', () => {
    const state = createInitialRuntimeState();
    const accounts: Account[] = [];
    const setState = vi.fn();

    const options = buildUiStateActionsOptions({
      state,
      accounts,
      sessionsPageSize: 10,
      setState,
    });

    expect(options.state).toBe(state);
    expect(options.accounts).toBe(accounts);
    expect(options.sessionsPageSize).toBe(10);
    expect(options.setState).toBe(setState);
    expect(sortedKeys(options)).toEqual(['accounts', 'sessionsPageSize', 'setState', 'state']);
  });

  test('buildAccountsDerivedViewOptions wires accounts derived view dependencies', () => {
    const state = createInitialRuntimeState();
    const accounts: Account[] = [];
    const accountsState = {
      isRefreshingAccountTelemetry: vi.fn(() => false),
    };

    const options = buildAccountsDerivedViewOptions({
      state,
      accounts,
      accountsState,
    });

    expect(options.state).toBe(state);
    expect(options.accounts).toBe(accounts);
    expect(options.isRefreshingAccountTelemetry).toBe(accountsState.isRefreshingAccountTelemetry);
    expect(sortedKeys(options)).toEqual(['accounts', 'isRefreshingAccountTelemetry', 'state']);
  });

  test('buildSelectedAccountActionsOptions wires selected-account action dependencies', () => {
    const setState = vi.fn();
    const accountsState = {
      toggleAccountPin: vi.fn(async () => {}),
      pauseAccount: vi.fn(async () => {}),
      reactivateAccount: vi.fn(async () => {}),
      deleteAccount: vi.fn(async () => true),
      refreshAccountTelemetry: vi.fn(async () => {}),
      refreshAccountToken: vi.fn(async () => {}),
      refreshAllAccountsTelemetry: vi.fn(async () => {}),
    };

    const options = buildSelectedAccountActionsOptions({
      selectedAccountDetailId: 'acc-alice',
      setState,
      accountsState,
    });

    expect(options.selectedAccountDetailId).toBe('acc-alice');
    expect(options.setState).toBe(setState);
    expect(options.toggleAccountPin).toBe(accountsState.toggleAccountPin);
    expect(options.pauseAccount).toBe(accountsState.pauseAccount);
    expect(options.reactivateAccount).toBe(accountsState.reactivateAccount);
    expect(options.deleteAccount).toBe(accountsState.deleteAccount);
    expect(options.refreshAccountTelemetry).toBe(accountsState.refreshAccountTelemetry);
    expect(options.refreshAccountToken).toBe(accountsState.refreshAccountToken);
    expect(options.refreshAllAccountsTelemetry).toBe(accountsState.refreshAllAccountsTelemetry);
    expect(sortedKeys(options)).toEqual([
      'deleteAccount',
      'pauseAccount',
      'reactivateAccount',
      'refreshAccountTelemetry',
      'refreshAccountToken',
      'refreshAllAccountsTelemetry',
      'selectedAccountDetailId',
      'setState',
      'toggleAccountPin',
    ]);
  });

  test('buildSessionsOptions wires sessions polling dependencies', () => {
    const setState = vi.fn();
    const mapRuntimeStickySession = vi.fn();
    const clampSessionsOffset = vi.fn((offset: number, totalCount: number, sessionsPageSize: number) =>
      Math.max(0, Math.min(offset, Math.max(0, totalCount - sessionsPageSize))),
    );
    const listStickySessionsRequest = vi.fn(async () => ({ generatedAtMs: Date.now(), sessions: [] }));
    const purgeStaleSessionsRequest = vi.fn(async () => ({ generatedAtMs: Date.now(), purged: 0 }));
    const pushRuntimeEvent = vi.fn();

    const options = buildSessionsOptions({
      refreshMs: 1000,
      sessionsRuntimeLimit: 250,
      sessionsPageSize: 10,
      setState,
      mapRuntimeStickySession,
      clampSessionsOffset,
      listStickySessionsRequest,
      purgeStaleSessionsRequest,
      pushRuntimeEvent,
    });

    expect(options.refreshMs).toBe(1000);
    expect(options.sessionsRuntimeLimit).toBe(250);
    expect(options.setState).toBe(setState);
    expect(options.mapRuntimeStickySession).toBe(mapRuntimeStickySession);
    expect(options.listStickySessionsRequest).toBe(listStickySessionsRequest);
    expect(options.purgeStaleSessionsRequest).toBe(purgeStaleSessionsRequest);
    expect(options.clampSessionsOffset(30, 50)).toBe(30);
    expect(clampSessionsOffset).toHaveBeenCalledWith(30, 50, 10);
    options.reportPollingError?.('sticky session polling failed; retrying');
    expect(pushRuntimeEvent).toHaveBeenCalledWith('sticky session polling failed; retrying', 'warn');
    options.reportPurgeError?.('sticky session purge failed');
    expect(pushRuntimeEvent).toHaveBeenCalledWith('sticky session purge failed', 'warn');
    expect(sortedKeys(options)).toEqual([
      'clampSessionsOffset',
      'listStickySessionsRequest',
      'mapRuntimeStickySession',
      'purgeStaleSessionsRequest',
      'refreshMs',
      'reportPollingError',
      'reportPurgeError',
      'sessionsRuntimeLimit',
      'setState',
    ]);
  });

  test('buildRequestLogOptions wires request log polling dependencies', () => {
    const setState = vi.fn();
    const mapRuntimeRequestLog = vi.fn();
    const shouldTriggerImmediateAccountsRefresh = vi.fn(() => true);
    const accountsState = {
      triggerFastAccountRefresh: vi.fn(),
    };
    const listRequestLogsRequest = vi.fn(async () => []);
    const pushRuntimeEvent = vi.fn();

    const options = buildRequestLogOptions({
      refreshMs: 500,
      requestLogsLimit: 100,
      setState,
      mapRuntimeRequestLog,
      shouldTriggerImmediateAccountsRefresh,
      accountsState,
      listRequestLogsRequest,
      pushRuntimeEvent,
    });

    expect(options.refreshMs).toBe(500);
    expect(options.requestLogsLimit).toBe(100);
    expect(options.setState).toBe(setState);
    expect(options.mapRuntimeRequestLog).toBe(mapRuntimeRequestLog);
    expect(options.shouldTriggerImmediateAccountsRefresh).toBe(shouldTriggerImmediateAccountsRefresh);
    expect(options.triggerFastAccountRefresh).toBe(accountsState.triggerFastAccountRefresh);
    expect(options.listRequestLogsRequest).toBe(listRequestLogsRequest);
    options.reportPollingError?.('request log polling failed; retrying');
    expect(pushRuntimeEvent).toHaveBeenCalledWith('request log polling failed; retrying', 'warn');
    expect(sortedKeys(options)).toEqual([
      'listRequestLogsRequest',
      'mapRuntimeRequestLog',
      'refreshMs',
      'reportPollingError',
      'requestLogsLimit',
      'setState',
      'shouldTriggerImmediateAccountsRefresh',
      'triggerFastAccountRefresh',
    ]);
  });
});
