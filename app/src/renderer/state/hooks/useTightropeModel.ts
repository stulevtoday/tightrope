import { useState } from 'react';
import { createInitialRuntimeState } from '../defaults';
import type { TightropeService } from '../../services/tightrope';
import { useAccountsDerivedView } from './useAccountsDerivedView';
import { useRuntimeEvents } from './useRuntimeEvents';
import { useSelectedAccountActions } from './useSelectedAccountActions';
import {
  buildTightropeModel,
  type TightropeModelAccountActionsBundle,
  type TightropeModelSettingsControls,
} from './tightropeModelComposition';
import { buildAccountActionsBundle, buildSettingsControls } from './tightropeModelBundleBuilders';
import {
  buildDialogActions,
  buildNavigationActions,
  buildRouterActions,
  buildRuntimeActions,
  buildSessionsAndLogsActions,
} from './tightropeModelActionBuilders';
import {
  buildAccountsDerivedViewOptions,
  buildSelectedAccountActionsOptions,
} from './tightropeModelHookOptionsBuilders';
import { useTightropeModelDomains } from './tightropeModelDomains';
import { buildStateDataBundle } from './tightropeModelStateDataBuilder';
import type { TightropeModel } from '../context/modelTypes';
import { formatNumber, stableSparklinePercents } from '../logic';

export function useTightropeModel(service: TightropeService): TightropeModel {
  const [state, setState] = useState(() => createInitialRuntimeState());
  const { pushRuntimeEvent } = useRuntimeEvents({ setState });

  const {
    settingsState,
    accountsState,
    sessionsState,
    oauthState,
    firewallState,
    runtimeDomain,
    clusterSyncState,
    settingsActions,
    navigationState,
    uiState,
  } = useTightropeModelDomains({
    state,
    setState,
    pushRuntimeEvent,
    service,
  });

  const accounts = accountsState.accounts;
  const routingModes = settingsState.routingModes;
  const dashboardSettings = settingsState.dashboardSettings;
  const settingsSaveInFlight = settingsState.settingsSaveInFlight;
  const hasUnsavedSettingsChanges = settingsState.hasUnsavedSettingsChanges;

  const accountsView = useAccountsDerivedView(
    buildAccountsDerivedViewOptions({
      state,
      accounts,
      accountsState,
    }),
  );
  const selectedAccountDetail = accountsView.selectedAccountDetail;
  const accountActions = useSelectedAccountActions(
    buildSelectedAccountActionsOptions({
      selectedAccountDetailId: selectedAccountDetail?.id ?? null,
      setState,
      accountsState,
    }),
  );

  const stateData = buildStateDataBundle({
    state,
    accountsState: {
      accounts,
      trafficClockMs: accountsState.trafficClockMs,
      trafficActiveWindowMs: accountsState.trafficActiveWindowMs,
    },
    settingsState: {
      routingModes,
      dashboardSettings,
      hasUnsavedSettingsChanges,
      settingsSaveInFlight,
    },
    firewallState: {
      firewallMode: firewallState.firewallMode,
      firewallEntries: firewallState.firewallEntries,
      firewallDraftIpAddress: firewallState.firewallDraftIpAddress,
    },
    clusterSyncState: {
      clusterStatus: clusterSyncState.clusterStatus,
      manualPeerAddress: clusterSyncState.manualPeerAddress,
    },
    navigationState: {
      settingsLeaveDialogOpen: navigationState.settingsLeaveDialogOpen,
    },
    accountsView: {
      filteredAccounts: accountsView.filteredAccounts,
      selectedAccountDetail,
      selectedAccountUsage24h: accountsView.selectedAccountUsage24h,
      isRefreshingSelectedAccountTelemetry: accountsView.isRefreshingSelectedAccountTelemetry,
      isRefreshingSelectedAccountToken: accountsState.isRefreshingAccountToken(selectedAccountDetail?.id ?? null),
      isRefreshingAllAccountTelemetry: accountsState.isRefreshingAllAccountTelemetry,
    },
    utils: {
      stableSparklinePercents,
      formatNumber,
    },
  });

  const navigationActions = buildNavigationActions({ navigationState });

  const routerActions = buildRouterActions({ uiState });

  const runtimeActions = buildRuntimeActions({
    settingsState,
    runtimeDomain,
    oauthState,
  });

  const settingsControls: TightropeModelSettingsControls = buildSettingsControls({
    settingsState,
    firewallState,
    clusterSyncState,
    settingsActions,
  });

  const dialogActions = buildDialogActions({ uiState });

  const accountActionsBundle: TightropeModelAccountActionsBundle = buildAccountActionsBundle({
    oauthState,
    uiState,
    accountActions,
  });

  const sessionsAndLogsActions = buildSessionsAndLogsActions({
    uiState: {
      ...uiState,
      purgeStaleSessions: sessionsState.purgeStaleSessions,
    },
  });

  const model = buildTightropeModel({
    stateData,
    navigationActions,
    routerActions,
    runtimeActions,
    settingsControls,
    dialogActions,
    accountActionsBundle,
    sessionsAndLogsActions,
  });

  return model;
}
