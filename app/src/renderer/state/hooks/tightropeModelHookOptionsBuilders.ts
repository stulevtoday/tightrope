import type { UseAccountsDerivedViewOptions } from './useAccountsDerivedView';
import type { UseAccountsOptions } from './useAccounts';
import type { UseClusterSyncOptions } from './useClusterSync';
import type { UseFirewallOptions } from './useFirewall';
import type { UseNavigationOptions } from './useNavigation';
import type { UseOAuthFlowOptions } from './useOAuthFlow';
import type { UseRequestLogOptions } from './useRequestLog';
import type { UseRuntimeStateOptions } from './useRuntimeState';
import type { UseSelectedAccountActionsOptions } from './useSelectedAccountActions';
import type { UseSettingsOptions } from './useSettings';
import type { UseSettingsSaveActionsOptions } from './useSettingsSaveActions';
import type { UseSessionsOptions } from './useSessions';
import type { UseUiStateActionsOptions } from './useUiStateActions';
import type { StatusNoticeLevel } from '../statusNotices';

interface BuildSettingsOptionsInput {
  stateTheme: UseSettingsOptions['stateTheme'];
  stateRoutingMode: UseSettingsOptions['stateRoutingMode'];
  setState: UseSettingsOptions['setState'];
  pushRuntimeEvent: UseSettingsOptions['pushRuntimeEvent'];
  service: Pick<UseSettingsOptions, 'getSettingsRequest' | 'updateSettingsRequest'>;
}

export function buildSettingsOptions(input: BuildSettingsOptionsInput): UseSettingsOptions {
  return {
    stateTheme: input.stateTheme,
    stateRoutingMode: input.stateRoutingMode,
    setState: input.setState,
    pushRuntimeEvent: input.pushRuntimeEvent,
    getSettingsRequest: input.service.getSettingsRequest,
    updateSettingsRequest: input.service.updateSettingsRequest,
  };
}

interface BuildAccountsOptionsInput {
  runtimeBind: UseAccountsOptions['runtimeBind'];
  pushRuntimeEvent: UseAccountsOptions['pushRuntimeEvent'];
  service: UseAccountsOptions['service'];
}

export function buildAccountsOptions(input: BuildAccountsOptionsInput): UseAccountsOptions {
  return {
    runtimeBind: input.runtimeBind,
    pushRuntimeEvent: input.pushRuntimeEvent,
    service: input.service,
  };
}

interface BuildOauthFlowOptionsInput {
  state: UseOAuthFlowOptions['state'];
  accounts: UseOAuthFlowOptions['accounts'];
  setState: UseOAuthFlowOptions['setState'];
  accountsState: {
    refreshAccountsFromNative: UseOAuthFlowOptions['refreshAccountsFromNative'];
    refreshUsageTelemetryAfterAccountAdd: UseOAuthFlowOptions['refreshUsageTelemetryAfterAccountAdd'];
  };
  pushRuntimeEvent: UseOAuthFlowOptions['pushRuntimeEvent'];
  service: Pick<
    UseOAuthFlowOptions,
    | 'oauthStartRequest'
    | 'oauthStatusRequest'
    | 'oauthStopRequest'
    | 'oauthRestartRequest'
    | 'oauthCompleteRequest'
    | 'oauthManualCallbackRequest'
    | 'importAccountRequest'
    | 'onOauthDeepLinkRequest'
  >;
}

export function buildOauthFlowOptions(input: BuildOauthFlowOptionsInput): UseOAuthFlowOptions {
  return {
    state: input.state,
    accounts: input.accounts,
    setState: input.setState,
    refreshAccountsFromNative: input.accountsState.refreshAccountsFromNative,
    refreshUsageTelemetryAfterAccountAdd: input.accountsState.refreshUsageTelemetryAfterAccountAdd,
    pushRuntimeEvent: input.pushRuntimeEvent,
    oauthStartRequest: input.service.oauthStartRequest,
    oauthStatusRequest: input.service.oauthStatusRequest,
    oauthStopRequest: input.service.oauthStopRequest,
    oauthRestartRequest: input.service.oauthRestartRequest,
    oauthCompleteRequest: input.service.oauthCompleteRequest,
    oauthManualCallbackRequest: input.service.oauthManualCallbackRequest,
    importAccountRequest: input.service.importAccountRequest,
    onOauthDeepLinkRequest: input.service.onOauthDeepLinkRequest,
  };
}

interface BuildFirewallOptionsInput {
  pushRuntimeEvent: UseFirewallOptions['pushRuntimeEvent'];
  listFirewallIpsRequest: UseFirewallOptions['listFirewallIpsRequest'];
  addFirewallIpRequest: UseFirewallOptions['addFirewallIpRequest'];
  removeFirewallIpRequest: UseFirewallOptions['removeFirewallIpRequest'];
}

export function buildFirewallOptions(input: BuildFirewallOptionsInput): UseFirewallOptions {
  return {
    pushRuntimeEvent: input.pushRuntimeEvent,
    listFirewallIpsRequest: input.listFirewallIpsRequest,
    addFirewallIpRequest: input.addFirewallIpRequest,
    removeFirewallIpRequest: input.removeFirewallIpRequest,
  };
}

interface BuildRuntimeStateOptionsInput {
  runtimeState: UseRuntimeStateOptions['runtimeState'];
  setState: UseRuntimeStateOptions['setState'];
  pushRuntimeEvent: UseRuntimeStateOptions['pushRuntimeEvent'];
  backendStatusRequest: UseRuntimeStateOptions['backendStatusRequest'];
  backendStartRequest: UseRuntimeStateOptions['backendStartRequest'];
  backendStopRequest: UseRuntimeStateOptions['backendStopRequest'];
}

export function buildRuntimeStateOptions(input: BuildRuntimeStateOptionsInput): UseRuntimeStateOptions {
  return {
    runtimeState: input.runtimeState,
    setState: input.setState,
    pushRuntimeEvent: input.pushRuntimeEvent,
    backendStatusRequest: input.backendStatusRequest,
    backendStartRequest: input.backendStartRequest,
    backendStopRequest: input.backendStopRequest,
  };
}

interface BuildClusterSyncOptionsInput {
  dashboardSettings: UseClusterSyncOptions['dashboardSettings'];
  appliedDashboardSettings: UseClusterSyncOptions['appliedDashboardSettings'];
  hasUnsavedSettingsChanges: UseClusterSyncOptions['hasUnsavedSettingsChanges'];
  makeSettingsUpdate: UseClusterSyncOptions['makeSettingsUpdate'];
  settingsState: {
    applyPersistedDashboardSettings: UseClusterSyncOptions['applyPersistedDashboardSettings'];
  };
  pushRuntimeEvent: UseClusterSyncOptions['pushRuntimeEvent'];
  service: Pick<
    UseClusterSyncOptions,
    | 'getClusterStatusRequest'
    | 'clusterEnableRequest'
    | 'clusterDisableRequest'
    | 'addPeerRequest'
    | 'removePeerRequest'
    | 'triggerSyncRequest'
    | 'updateSettingsRequest'
  >;
}

export function buildClusterSyncOptions(input: BuildClusterSyncOptionsInput): UseClusterSyncOptions {
  return {
    dashboardSettings: input.dashboardSettings,
    appliedDashboardSettings: input.appliedDashboardSettings,
    hasUnsavedSettingsChanges: input.hasUnsavedSettingsChanges,
    makeSettingsUpdate: input.makeSettingsUpdate,
    applyPersistedDashboardSettings: input.settingsState.applyPersistedDashboardSettings,
    pushRuntimeEvent: input.pushRuntimeEvent,
    getClusterStatusRequest: input.service.getClusterStatusRequest,
    clusterEnableRequest: input.service.clusterEnableRequest,
    clusterDisableRequest: input.service.clusterDisableRequest,
    addPeerRequest: input.service.addPeerRequest,
    removePeerRequest: input.service.removePeerRequest,
    triggerSyncRequest: input.service.triggerSyncRequest,
    updateSettingsRequest: input.service.updateSettingsRequest,
  };
}

interface BuildSettingsSaveActionsOptionsInput {
  settingsState: UseSettingsSaveActionsOptions['settingsState'];
  clusterSyncState: {
    clusterStatus: {
      enabled: UseSettingsSaveActionsOptions['clusterSyncEnabled'];
    };
    reconfigureSyncCluster: UseSettingsSaveActionsOptions['reconfigureSyncCluster'];
  };
}

export function buildSettingsSaveActionsOptions(
  input: BuildSettingsSaveActionsOptionsInput,
): UseSettingsSaveActionsOptions {
  return {
    settingsState: input.settingsState,
    clusterSyncEnabled: input.clusterSyncState.clusterStatus.enabled,
    reconfigureSyncCluster: input.clusterSyncState.reconfigureSyncCluster,
  };
}

interface BuildNavigationOptionsInput {
  currentPage: UseNavigationOptions['currentPage'];
  hasUnsavedSettingsChanges: UseNavigationOptions['hasUnsavedSettingsChanges'];
  settingsSaveInFlight: UseNavigationOptions['settingsSaveInFlight'];
  setState: UseNavigationOptions['setState'];
  settingsActions: {
    discardDashboardSettingsChanges: UseNavigationOptions['discardDashboardSettingsChanges'];
    saveDashboardSettings: UseNavigationOptions['saveDashboardSettings'];
  };
}

export function buildNavigationOptions(input: BuildNavigationOptionsInput): UseNavigationOptions {
  return {
    currentPage: input.currentPage,
    hasUnsavedSettingsChanges: input.hasUnsavedSettingsChanges,
    settingsSaveInFlight: input.settingsSaveInFlight,
    setState: input.setState,
    discardDashboardSettingsChanges: input.settingsActions.discardDashboardSettingsChanges,
    saveDashboardSettings: input.settingsActions.saveDashboardSettings,
  };
}

interface BuildUiStateActionsOptionsInput {
  state: UseUiStateActionsOptions['state'];
  accounts: UseUiStateActionsOptions['accounts'];
  sessionsPageSize: UseUiStateActionsOptions['sessionsPageSize'];
  setState: UseUiStateActionsOptions['setState'];
}

export function buildUiStateActionsOptions(input: BuildUiStateActionsOptionsInput): UseUiStateActionsOptions {
  return {
    state: input.state,
    accounts: input.accounts,
    sessionsPageSize: input.sessionsPageSize,
    setState: input.setState,
  };
}

interface BuildAccountsDerivedViewOptionsInput {
  state: UseAccountsDerivedViewOptions['state'];
  accounts: UseAccountsDerivedViewOptions['accounts'];
  accountsState: {
    isRefreshingAccountTelemetry: UseAccountsDerivedViewOptions['isRefreshingAccountTelemetry'];
  };
}

export function buildAccountsDerivedViewOptions(
  input: BuildAccountsDerivedViewOptionsInput,
): UseAccountsDerivedViewOptions {
  return {
    state: input.state,
    accounts: input.accounts,
    isRefreshingAccountTelemetry: input.accountsState.isRefreshingAccountTelemetry,
  };
}

interface BuildSelectedAccountActionsOptionsInput {
  selectedAccountDetailId: UseSelectedAccountActionsOptions['selectedAccountDetailId'];
  setState: UseSelectedAccountActionsOptions['setState'];
  accountsState: {
    toggleAccountPin: UseSelectedAccountActionsOptions['toggleAccountPin'];
    pauseAccount: UseSelectedAccountActionsOptions['pauseAccount'];
    reactivateAccount: UseSelectedAccountActionsOptions['reactivateAccount'];
    deleteAccount: UseSelectedAccountActionsOptions['deleteAccount'];
    refreshAccountTelemetry: UseSelectedAccountActionsOptions['refreshAccountTelemetry'];
    refreshAccountToken: UseSelectedAccountActionsOptions['refreshAccountToken'];
    refreshAllAccountsTelemetry: UseSelectedAccountActionsOptions['refreshAllAccountsTelemetry'];
  };
}

export function buildSelectedAccountActionsOptions(
  input: BuildSelectedAccountActionsOptionsInput,
): UseSelectedAccountActionsOptions {
  return {
    selectedAccountDetailId: input.selectedAccountDetailId,
    setState: input.setState,
    toggleAccountPin: input.accountsState.toggleAccountPin,
    pauseAccount: input.accountsState.pauseAccount,
    reactivateAccount: input.accountsState.reactivateAccount,
    deleteAccount: input.accountsState.deleteAccount,
    refreshAccountTelemetry: input.accountsState.refreshAccountTelemetry,
    refreshAccountToken: input.accountsState.refreshAccountToken,
    refreshAllAccountsTelemetry: input.accountsState.refreshAllAccountsTelemetry,
  };
}

interface BuildSessionsOptionsInput {
  refreshMs: UseSessionsOptions['refreshMs'];
  sessionsRuntimeLimit: UseSessionsOptions['sessionsRuntimeLimit'];
  sessionsPageSize: number;
  setState: UseSessionsOptions['setState'];
  mapRuntimeStickySession: UseSessionsOptions['mapRuntimeStickySession'];
  clampSessionsOffset: (offset: number, totalCount: number, sessionsPageSize: number) => number;
  listStickySessionsRequest: UseSessionsOptions['listStickySessionsRequest'];
  purgeStaleSessionsRequest: UseSessionsOptions['purgeStaleSessionsRequest'];
  pushRuntimeEvent: (text: string, level?: StatusNoticeLevel) => void;
}

export function buildSessionsOptions(input: BuildSessionsOptionsInput): UseSessionsOptions {
  return {
    refreshMs: input.refreshMs,
    sessionsRuntimeLimit: input.sessionsRuntimeLimit,
    setState: input.setState,
    mapRuntimeStickySession: input.mapRuntimeStickySession,
    listStickySessionsRequest: input.listStickySessionsRequest,
    purgeStaleSessionsRequest: input.purgeStaleSessionsRequest,
    clampSessionsOffset: (offset: number, totalCount: number) =>
      input.clampSessionsOffset(offset, totalCount, input.sessionsPageSize),
    reportPollingError: (message: string) => input.pushRuntimeEvent(message, 'warn'),
    reportPurgeError: (message: string) => input.pushRuntimeEvent(message, 'warn'),
  };
}

interface BuildRequestLogOptionsInput {
  refreshMs: UseRequestLogOptions['refreshMs'];
  requestLogsLimit: UseRequestLogOptions['requestLogsLimit'];
  setState: UseRequestLogOptions['setState'];
  mapRuntimeRequestLog: UseRequestLogOptions['mapRuntimeRequestLog'];
  shouldTriggerImmediateAccountsRefresh: UseRequestLogOptions['shouldTriggerImmediateAccountsRefresh'];
  accountsState: {
    triggerFastAccountRefresh: UseRequestLogOptions['triggerFastAccountRefresh'];
  };
  listRequestLogsRequest: UseRequestLogOptions['listRequestLogsRequest'];
  pushRuntimeEvent: (text: string, level?: StatusNoticeLevel) => void;
}

export function buildRequestLogOptions(input: BuildRequestLogOptionsInput): UseRequestLogOptions {
  return {
    refreshMs: input.refreshMs,
    requestLogsLimit: input.requestLogsLimit,
    setState: input.setState,
    mapRuntimeRequestLog: input.mapRuntimeRequestLog,
    shouldTriggerImmediateAccountsRefresh: input.shouldTriggerImmediateAccountsRefresh,
    triggerFastAccountRefresh: input.accountsState.triggerFastAccountRefresh,
    listRequestLogsRequest: input.listRequestLogsRequest,
    reportPollingError: (message: string) => input.pushRuntimeEvent(message, 'warn'),
  };
}
