import { useEffect, useRef } from 'react';
import i18next from 'i18next';
import type { Dispatch, SetStateAction } from 'react';
import type { TightropeService } from '../../services/tightrope';
import type { Account, AppRuntimeState, SyncEvent } from '../../shared/types';
import {
  REQUEST_LOGS_LIMIT,
  REQUEST_LOGS_POLL_MS,
  SESSIONS_PAGE_SIZE,
  SESSIONS_POLL_MS,
  SESSIONS_RUNTIME_LIMIT,
} from '../config';
import type { StatusNoticeLevel } from '../statusNotices';
import { useAccounts } from './useAccounts';
import { useBootstrap } from './useBootstrap';
import { buildBootstrapOptions } from './bootstrapOptionsBuilder';
import { useClusterSync } from './useClusterSync';
import { useFirewall } from './useFirewall';
import { useNavigation } from './useNavigation';
import { useOAuthFlow } from './useOAuthFlow';
import { useRequestLog } from './useRequestLog';
import { runtimeRequestLogToRouteRow, shouldTriggerImmediateAccountsRefresh } from './useRequestLogMapping';
import { useRuntimeState } from './useRuntimeState';
import { makeSettingsUpdate, useSettings } from './useSettings';
import { useSettingsSaveActions } from './useSettingsSaveActions';
import { useSessions } from './useSessions';
import { clampSessionsOffset, mapRuntimeStickySessionToUiSession } from './useSessionsMapping';
import {
  buildAccountsOptions,
  buildClusterSyncOptions,
  buildFirewallOptions,
  buildNavigationOptions,
  buildOauthFlowOptions,
  buildRequestLogOptions,
  buildRuntimeStateOptions,
  buildSettingsOptions,
  buildSettingsSaveActionsOptions,
  buildSessionsOptions,
  buildUiStateActionsOptions,
} from './tightropeModelHookOptionsBuilders';
import { useUiStateActions } from './useUiStateActions';

type SettingsDomainState = ReturnType<typeof useSettings>;
type AccountsDomainState = ReturnType<typeof useAccounts>;
type SessionsDomainState = ReturnType<typeof useSessions>;
type RequestLogDomainState = ReturnType<typeof useRequestLog>;
type OAuthDomainState = ReturnType<typeof useOAuthFlow>;
type FirewallDomainState = ReturnType<typeof useFirewall>;
type RuntimeDomainState = ReturnType<typeof useRuntimeState>;
type ClusterSyncDomainState = ReturnType<typeof useClusterSync>;
type SettingsActionsDomainState = ReturnType<typeof useSettingsSaveActions>;
type NavigationDomainState = ReturnType<typeof useNavigation>;
type UiStateDomainState = ReturnType<typeof useUiStateActions>;

interface UseTightropeModelDomainsInput {
  state: AppRuntimeState;
  setState: Dispatch<SetStateAction<AppRuntimeState>>;
  pushRuntimeEvent: (text: string, level?: StatusNoticeLevel) => void;
  service: TightropeService;
}

export interface TightropeModelDomains {
  settingsState: SettingsDomainState;
  accountsState: AccountsDomainState;
  sessionsState: SessionsDomainState;
  requestLogState: RequestLogDomainState;
  oauthState: OAuthDomainState;
  firewallState: FirewallDomainState;
  runtimeDomain: RuntimeDomainState;
  clusterSyncState: ClusterSyncDomainState;
  settingsActions: SettingsActionsDomainState;
  navigationState: NavigationDomainState;
  uiState: UiStateDomainState;
}

interface QuotaSignalTrack {
  key: string;
  usedPercent: number | null;
  windowLabel: string;
  resetAtMs: number | null;
}

const QUOTA_MILESTONE_STEPS = [75, 90, 95, 100] as const;
const QUOTA_MILESTONE_REARM_PERCENT = 50;

function clampPercent(value: number): number {
  return Math.max(0, Math.min(100, Math.round(value)));
}

function runtimeSignalLevelToNoticeLevel(level: 'info' | 'success' | 'warn' | 'error'): StatusNoticeLevel {
  if (level === 'success' || level === 'warn' || level === 'error') {
    return level;
  }
  return 'info';
}

function usageMilestone(usedPercent: number): number {
  let current = 0;
  for (const milestone of QUOTA_MILESTONE_STEPS) {
    if (usedPercent >= milestone) {
      current = milestone;
    }
  }
  return current;
}

function quotaSignalTracks(account: Account): QuotaSignalTrack[] {
  const tracks: QuotaSignalTrack[] = [];
  if (account.hasPrimaryQuota) {
    tracks.push({
      key: `${account.id}:primary`,
      usedPercent: clampPercent(account.quotaPrimary),
      windowLabel: account.plan === 'free' ? i18next.t('common.weekly') : i18next.t('common.hour_window', { hours: 5 }),
      resetAtMs: account.quotaPrimaryResetAtMs ?? null,
    });
  }
  if (account.plan !== 'free' && account.hasSecondaryQuota) {
    tracks.push({
      key: `${account.id}:secondary`,
      usedPercent: clampPercent(account.quotaSecondary),
      windowLabel: i18next.t('common.weekly'),
      resetAtMs: account.quotaSecondaryResetAtMs ?? null,
    });
  }
  return tracks;
}

export function useTightropeModelDomains(input: UseTightropeModelDomainsInput): TightropeModelDomains {
  const runtimeSignalsInitializedRef = useRef(false);
  const routedAccountRef = useRef<string | null>(null);
  const quotaMilestoneByKeyRef = useRef<Map<string, number>>(new Map());
  const quotaResetByKeyRef = useRef<Map<string, number | null>>(new Map());

  const settingsState = useSettings(
    buildSettingsOptions({
      stateTheme: input.state.theme,
      stateRoutingMode: input.state.routingMode,
      setState: input.setState,
      pushRuntimeEvent: input.pushRuntimeEvent,
      service: input.service,
    }),
  );

  const hasUnsavedSettingsChanges = settingsState.hasUnsavedSettingsChanges;
  const settingsSaveInFlight = settingsState.settingsSaveInFlight;

  const accountsState = useAccounts(
    buildAccountsOptions({
      runtimeBind: input.state.runtimeState.bind,
      pushRuntimeEvent: input.pushRuntimeEvent,
      service: {
        listAccountsRequest: input.service.listAccountsRequest,
        listAccountTrafficRequest: input.service.listAccountTrafficRequest,
        refreshAccountUsageTelemetryRequest: input.service.refreshAccountUsageTelemetryRequest,
        refreshAccountTokenRequest: input.service.refreshAccountTokenRequest,
        pinAccountRequest: input.service.pinAccountRequest,
        unpinAccountRequest: input.service.unpinAccountRequest,
        pauseAccountRequest: input.service.pauseAccountRequest,
        reactivateAccountRequest: input.service.reactivateAccountRequest,
        deleteAccountRequest: input.service.deleteAccountRequest,
      },
    }),
  );

  const sessionsState = useSessions(
    buildSessionsOptions({
      refreshMs: SESSIONS_POLL_MS,
      sessionsRuntimeLimit: SESSIONS_RUNTIME_LIMIT,
      sessionsPageSize: SESSIONS_PAGE_SIZE,
      setState: input.setState,
      mapRuntimeStickySession: mapRuntimeStickySessionToUiSession,
      clampSessionsOffset,
      listStickySessionsRequest: input.service.listStickySessionsRequest,
      purgeStaleSessionsRequest: input.service.purgeStaleSessionsRequest,
      pushRuntimeEvent: input.pushRuntimeEvent,
    }),
  );

  const requestLogState = useRequestLog(
    buildRequestLogOptions({
      refreshMs: REQUEST_LOGS_POLL_MS,
      requestLogsLimit: REQUEST_LOGS_LIMIT,
      setState: input.setState,
      mapRuntimeRequestLog: runtimeRequestLogToRouteRow,
      shouldTriggerImmediateAccountsRefresh,
      accountsState,
      listRequestLogsRequest: input.service.listRequestLogsRequest,
      pushRuntimeEvent: input.pushRuntimeEvent,
    }),
  );

  const accounts = accountsState.accounts;
  const dashboardSettings = settingsState.dashboardSettings;
  const appliedDashboardSettings = settingsState.appliedDashboardSettings;

  const oauthState = useOAuthFlow(
    buildOauthFlowOptions({
      state: input.state,
      accounts,
      setState: input.setState,
      accountsState,
      pushRuntimeEvent: input.pushRuntimeEvent,
      service: input.service,
    }),
  );

  const firewallState = useFirewall(buildFirewallOptions({
    pushRuntimeEvent: input.pushRuntimeEvent,
    listFirewallIpsRequest: input.service.listFirewallIpsRequest,
    addFirewallIpRequest: input.service.addFirewallIpRequest,
    removeFirewallIpRequest: input.service.removeFirewallIpRequest,
  }));

  const runtimeDomain = useRuntimeState(
    buildRuntimeStateOptions({
      runtimeState: input.state.runtimeState,
      setState: input.setState,
      pushRuntimeEvent: input.pushRuntimeEvent,
      backendStatusRequest: input.service.backendStatusRequest,
      backendStartRequest: input.service.backendStartRequest,
      backendStopRequest: input.service.backendStopRequest,
    }),
  );

  const clusterSyncState = useClusterSync(
    buildClusterSyncOptions({
      dashboardSettings,
      appliedDashboardSettings,
      hasUnsavedSettingsChanges,
      makeSettingsUpdate,
      settingsState,
      pushRuntimeEvent: input.pushRuntimeEvent,
      service: input.service,
    }),
  );

  const settingsActions = useSettingsSaveActions(
    buildSettingsSaveActionsOptions({
      settingsState,
      clusterSyncState,
    }),
  );

  const navigationState = useNavigation(
    buildNavigationOptions({
      currentPage: input.state.currentPage,
      hasUnsavedSettingsChanges,
      settingsSaveInFlight,
      setState: input.setState,
      settingsActions,
    }),
  );

  const uiState = useUiStateActions(
    buildUiStateActionsOptions({
      state: input.state,
      accounts,
      sessionsPageSize: SESSIONS_PAGE_SIZE,
      setState: input.setState,
    }),
  );

  useBootstrap(
    buildBootstrapOptions({
      accountsState,
      runtimeDomain,
      sessionsState,
      requestLogState,
      settingsState,
      firewallState,
      clusterSyncState,
      oauthState,
      pushRuntimeEvent: input.pushRuntimeEvent,
    }),
  );

  const applyTrafficFrame = accountsState.applyTrafficFrame;
  useEffect(() => {
    const accountNameById = new Map(accountsState.accounts.map((account) => [account.id, account.name]));
    const unsubscribe = input.service.onSyncEventRequest((event: SyncEvent) => {
      if (event.type === 'account_traffic') {
        applyTrafficFrame({
          accountId: event.account_id,
          upBytes: event.up_bytes,
          downBytes: event.down_bytes,
          lastUpAtMs: event.last_up_at_ms,
          lastDownAtMs: event.last_down_at_ms,
        });
        return;
      }
      if (event.type !== 'runtime_signal') {
        return;
      }

      if (event.code === 'route_account_selected') {
        const nextRoutedAccountId = typeof event.account_id === 'string' ? event.account_id.trim() : '';
        if (!nextRoutedAccountId) {
          return;
        }
        const previousRoutedAccountId = routedAccountRef.current;
        if (previousRoutedAccountId && previousRoutedAccountId !== nextRoutedAccountId) {
          const previousName = accountNameById.get(previousRoutedAccountId) ?? previousRoutedAccountId;
          const nextName = accountNameById.get(nextRoutedAccountId) ?? nextRoutedAccountId;
          input.pushRuntimeEvent(`route switched ${previousName} -> ${nextName}`, 'success');
        }
        routedAccountRef.current = nextRoutedAccountId;
        input.setState((previous) => {
          if (previous.currentRoutedAccountId === nextRoutedAccountId) {
            return previous;
          }
          return { ...previous, currentRoutedAccountId: nextRoutedAccountId };
        });
        return;
      }

      const message = typeof event.message === 'string' ? event.message.trim() : '';
      if (!message) {
        return;
      }
      const accountId = typeof event.account_id === 'string' ? event.account_id.trim() : '';
      const accountName = accountId ? (accountNameById.get(accountId) ?? accountId) : '';
      const renderedMessage = accountName ? `${message} (${accountName})` : message;
      input.pushRuntimeEvent(renderedMessage, runtimeSignalLevelToNoticeLevel(event.level));
    });
    return unsubscribe ?? undefined;
  }, [accountsState.accounts, input.service, input.pushRuntimeEvent, applyTrafficFrame]);

  useEffect(() => {
    const accounts = accountsState.accounts;

    if (!runtimeSignalsInitializedRef.current) {
      const initialMilestones = new Map<string, number>();
      const initialResets = new Map<string, number | null>();
      for (const account of accounts) {
        for (const track of quotaSignalTracks(account)) {
          initialMilestones.set(track.key, track.usedPercent === null ? 0 : usageMilestone(track.usedPercent));
          initialResets.set(track.key, track.resetAtMs);
        }
      }
      quotaMilestoneByKeyRef.current = initialMilestones;
      quotaResetByKeyRef.current = initialResets;

      runtimeSignalsInitializedRef.current = true;
      return;
    }

    const nextQuotaKeys = new Set<string>();
    for (const account of accounts) {
      for (const track of quotaSignalTracks(account)) {
        nextQuotaKeys.add(track.key);
        const usedPercent = track.usedPercent;
        if (usedPercent === null) {
          continue;
        }

        const previousResetAt = quotaResetByKeyRef.current.get(track.key) ?? null;
        const resetChanged = previousResetAt !== null && track.resetAtMs !== previousResetAt;
        let previousMilestone = quotaMilestoneByKeyRef.current.get(track.key) ?? 0;
        if (resetChanged || usedPercent <= QUOTA_MILESTONE_REARM_PERCENT) {
          previousMilestone = 0;
        }

        const currentMilestone = usageMilestone(usedPercent);
        if (currentMilestone > previousMilestone && currentMilestone >= QUOTA_MILESTONE_STEPS[0]) {
          const level: StatusNoticeLevel = currentMilestone >= 95 ? 'error' : 'warn';
          input.pushRuntimeEvent(
            i18next.t('status.quota_milestone', { account: account.name, window: track.windowLabel, percent: currentMilestone }),
            level,
          );
        }

        quotaMilestoneByKeyRef.current.set(track.key, Math.max(previousMilestone, currentMilestone));
        quotaResetByKeyRef.current.set(track.key, track.resetAtMs);
      }
    }

    for (const key of Array.from(quotaMilestoneByKeyRef.current.keys())) {
      if (!nextQuotaKeys.has(key)) {
        quotaMilestoneByKeyRef.current.delete(key);
      }
    }
    for (const key of Array.from(quotaResetByKeyRef.current.keys())) {
      if (!nextQuotaKeys.has(key)) {
        quotaResetByKeyRef.current.delete(key);
      }
    }
  }, [accountsState.accounts, input.pushRuntimeEvent]);

  return {
    settingsState,
    accountsState,
    sessionsState,
    requestLogState,
    oauthState,
    firewallState,
    runtimeDomain,
    clusterSyncState,
    settingsActions,
    navigationState,
    uiState,
  };
}
