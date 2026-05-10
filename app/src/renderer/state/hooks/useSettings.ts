import { useEffect, useReducer, useRef, useState, type Dispatch, type SetStateAction } from 'react';
import i18next from 'i18next';
import { DEFAULT_ROUTING_MODES } from '../defaults';
import type { TightropeService } from '../../services/tightrope';
import type {
  AppRuntimeState,
  DashboardSettings,
  DashboardSettingsUpdate,
  RoutingMode,
  SyncConflictResolution,
  ThemeMode,
  UpstreamStreamTransport,
} from '../../shared/types';
import { reportWarn } from '../errors';
import type { StatusNoticeLevel } from '../statusNotices';
import { boundedInteger, buildDashboardSettingsUpdate, buildSyncSettingsFingerprint } from './useSettingsHelpers';
import {
  buildSyncMinSupportedSchemaVersionPatch,
  buildSyncSchemaVersionPatch,
  clampUnitInterval,
  headroomWeightKeyToField,
  normalizeSyncClusterName,
  scoringWeightKeyToField,
} from './useSettingsMutators';
import { createInitialSettingsReducerState, settingsReducer } from './useSettingsReducer';

export const defaultDashboardSettings: DashboardSettings = {
  theme: 'auto',
  stickyThreadsEnabled: false,
  upstreamStreamTransport: 'default',
  preferEarlierResetAccounts: false,
  routingStrategy: 'weighted_round_robin',
  strictLockPoolContinuations: false,
  lockedRoutingAccountIds: [],
  openaiCacheAffinityMaxAgeSeconds: 300,
  importWithoutOverwrite: false,
  totpRequiredOnLogin: false,
  totpConfigured: false,
  apiKeyAuthEnabled: false,
  routingHeadroomWeightPrimary: 0.35,
  routingHeadroomWeightSecondary: 0.65,
  routingScoreAlpha: 0.3,
  routingScoreBeta: 0.25,
  routingScoreGamma: 0.2,
  routingScoreDelta: 0.2,
  routingScoreZeta: 0.05,
  routingScoreEta: 1,
  routingSuccessRateRho: 2,
  routingPlanModelPricingUsdPerMillion: '',
  syncClusterName: 'default',
  syncSiteId: 1,
  syncPort: 9400,
  syncDiscoveryEnabled: true,
  syncIntervalSeconds: 5,
  syncConflictResolution: 'lww',
  syncJournalRetentionDays: 30,
  syncTlsEnabled: true,
  syncRequireHandshakeAuth: true,
  syncClusterSharedSecret: '',
  syncTlsVerifyPeer: true,
  syncTlsCaCertificatePath: '',
  syncTlsCertificateChainPath: '',
  syncTlsPrivateKeyPath: '',
  syncTlsPinnedPeerCertificateSha256: '',
  syncSchemaVersion: 1,
  syncMinSupportedSchemaVersion: 1,
  syncAllowSchemaDowngrade: false,
  syncPeerProbeEnabled: true,
  syncPeerProbeIntervalMs: 5000,
  syncPeerProbeTimeoutMs: 500,
  syncPeerProbeMaxPerRefresh: 2,
  syncPeerProbeFailClosed: true,
  syncPeerProbeFailClosedFailures: 3,
};

function cloneRoutingModes(): RoutingMode[] {
  return DEFAULT_ROUTING_MODES.map((mode) => ({
    ...mode,
    params: mode.params ? { ...mode.params } : undefined,
  }));
}

export function makeSettingsUpdate(settings: DashboardSettings): DashboardSettingsUpdate {
  return buildDashboardSettingsUpdate(settings);
}

export function settingsScoringModel(settings: DashboardSettings, eps: number) {
  return {
    wp: settings.routingHeadroomWeightPrimary,
    ws: settings.routingHeadroomWeightSecondary,
    alpha: settings.routingScoreAlpha,
    beta: settings.routingScoreBeta,
    gamma: settings.routingScoreGamma,
    delta: settings.routingScoreDelta,
    zeta: settings.routingScoreZeta,
    eta: settings.routingScoreEta,
    eps,
  };
}

export function dashboardSettingsEqual(left: DashboardSettings, right: DashboardSettings): boolean {
  return JSON.stringify(makeSettingsUpdate(left)) === JSON.stringify(makeSettingsUpdate(right));
}

export function syncSettingsFingerprint(settings: DashboardSettings): string {
  return buildSyncSettingsFingerprint(settings);
}

interface SaveDashboardSettingsResult {
  saved: boolean;
  previousApplied: DashboardSettings | null;
  updated: DashboardSettings | null;
}

export interface UseSettingsOptions {
  stateTheme: ThemeMode;
  stateRoutingMode: string;
  setState: Dispatch<SetStateAction<AppRuntimeState>>;
  pushRuntimeEvent: (text: string, level?: StatusNoticeLevel) => void;
  getSettingsRequest: TightropeService['getSettingsRequest'];
  updateSettingsRequest: TightropeService['updateSettingsRequest'];
}

interface UseSettingsResult {
  routingModes: RoutingMode[];
  dashboardSettings: DashboardSettings;
  appliedDashboardSettings: DashboardSettings;
  settingsSaveInFlight: boolean;
  hasUnsavedSettingsChanges: boolean;
  applyPersistedDashboardSettings: (nextSettings: DashboardSettings, preserveDraft?: boolean) => void;
  refreshDashboardSettingsFromNative: () => Promise<void>;
  discardDashboardSettingsChanges: () => void;
  saveDashboardSettings: () => Promise<SaveDashboardSettingsResult>;
  saveSettingsChanges: () => Promise<boolean>;
  setRoutingMode: (nextMode: string) => void;
  setScoringWeight: (key: 'alpha' | 'beta' | 'gamma' | 'delta' | 'zeta' | 'eta', value: number) => void;
  setHeadroomWeight: (key: 'wp' | 'ws', value: number) => void;
  setStrategyParam: (modeId: string, key: string, value: number) => void;
  setTheme: (theme: ThemeMode) => void;
  setUpstreamStreamTransport: (transport: UpstreamStreamTransport) => void;
  setStickyThreadsEnabled: (enabled: boolean) => void;
  setPreferEarlierResetAccounts: (enabled: boolean) => void;
  setStrictLockPoolContinuations: (enabled: boolean) => void;
  updateLockedRoutingAccountIds: (accountIds: string[]) => Promise<boolean>;
  setOpenaiCacheAffinityMaxAgeSeconds: (seconds: number) => void;
  setImportWithoutOverwrite: (enabled: boolean) => void;
  setRoutingPlanModelPricingUsdPerMillion: (value: string) => void;
  setSyncClusterName: (clusterName: string) => void;
  setSyncSiteId: (siteId: number) => void;
  setSyncPort: (port: number) => void;
  setSyncDiscoveryEnabled: (enabled: boolean) => void;
  setSyncIntervalSeconds: (seconds: number) => void;
  setSyncConflictResolution: (strategy: SyncConflictResolution) => void;
  setSyncJournalRetentionDays: (days: number) => void;
  setSyncTlsEnabled: (enabled: boolean) => void;
  setSyncRequireHandshakeAuth: (enabled: boolean) => void;
  setSyncClusterSharedSecret: (secret: string) => void;
  setSyncTlsVerifyPeer: (enabled: boolean) => void;
  setSyncTlsCaCertificatePath: (path: string) => void;
  setSyncTlsCertificateChainPath: (path: string) => void;
  setSyncTlsPrivateKeyPath: (path: string) => void;
  setSyncTlsPinnedPeerCertificateSha256: (value: string) => void;
  setSyncSchemaVersion: (version: number) => void;
  setSyncMinSupportedSchemaVersion: (version: number) => void;
  setSyncAllowSchemaDowngrade: (enabled: boolean) => void;
  setSyncPeerProbeEnabled: (enabled: boolean) => void;
  setSyncPeerProbeIntervalMs: (value: number) => void;
  setSyncPeerProbeTimeoutMs: (value: number) => void;
  setSyncPeerProbeMaxPerRefresh: (value: number) => void;
  setSyncPeerProbeFailClosed: (enabled: boolean) => void;
  setSyncPeerProbeFailClosedFailures: (value: number) => void;
}

export function useSettings(options: UseSettingsOptions): UseSettingsResult {
  const { getSettingsRequest, updateSettingsRequest } = options;
  const [routingModes] = useState<RoutingMode[]>(() => cloneRoutingModes());
  const [state, dispatch] = useReducer(settingsReducer, createInitialSettingsReducerState(defaultDashboardSettings));
  const stateRef = useRef(state);

  useEffect(() => {
    stateRef.current = state;
  }, [state]);

  const dashboardSettings = state.dashboardSettings;
  const appliedDashboardSettings = state.appliedDashboardSettings;
  const settingsSaveInFlight = state.settingsSaveInFlight;

  function applyDraftDashboardSettings(nextSettings: DashboardSettings): void {
    stateRef.current = settingsReducer(stateRef.current, { type: 'apply_draft', nextSettings });
    dispatch({ type: 'apply_draft', nextSettings });
  }

  function applyAppliedDashboardSettings(nextSettings: DashboardSettings): void {
    stateRef.current = settingsReducer(stateRef.current, { type: 'apply_applied', nextSettings });
    dispatch({ type: 'apply_applied', nextSettings });
    options.setState((previous) => ({
      ...previous,
      theme: nextSettings.theme,
      routingMode: nextSettings.routingStrategy,
      scoringModel: settingsScoringModel(nextSettings, previous.scoringModel.eps),
    }));
  }

  function applyPersistedDashboardSettings(nextSettings: DashboardSettings, preserveDraft = false): void {
    applyAppliedDashboardSettings(nextSettings);
    if (!preserveDraft) {
      applyDraftDashboardSettings(nextSettings);
    }
  }

  async function refreshDashboardSettingsFromNative(): Promise<void> {
    const nextSettings = await getSettingsRequest();
    if (!nextSettings) {
      return;
    }
    if (!nextSettings.theme) {
      nextSettings.theme = options.stateTheme;
    }
    if (typeof nextSettings.strictLockPoolContinuations !== 'boolean') {
      nextSettings.strictLockPoolContinuations = false;
    }
    if (!Array.isArray(nextSettings.lockedRoutingAccountIds)) {
      nextSettings.lockedRoutingAccountIds = [];
    }
    if (!routingModes.some((mode) => mode.id === nextSettings.routingStrategy)) {
      nextSettings.routingStrategy = options.stateRoutingMode;
    }
    applyPersistedDashboardSettings(nextSettings);
  }

  function applyDashboardSettingsPatch(patch: DashboardSettingsUpdate): void {
    const nextSettings = {
      ...stateRef.current.dashboardSettings,
      ...patch,
    };
    applyDraftDashboardSettings(nextSettings);
  }

  function applySingleSettingPatch<Key extends keyof DashboardSettingsUpdate>(
    key: Key,
    value: DashboardSettingsUpdate[Key],
  ): void {
    applyDashboardSettingsPatch({ [key]: value } as Pick<DashboardSettingsUpdate, Key>);
  }

  function discardDashboardSettingsChanges(): void {
    applyDraftDashboardSettings(stateRef.current.appliedDashboardSettings);
  }

  async function saveDashboardSettings(): Promise<SaveDashboardSettingsResult> {
    if (settingsSaveInFlight) {
      return { saved: false, previousApplied: null, updated: null };
    }

    const previousApplied = stateRef.current.appliedDashboardSettings;
    const draft = stateRef.current.dashboardSettings;
    stateRef.current = settingsReducer(stateRef.current, { type: 'set_saving', value: true });
    dispatch({ type: 'set_saving', value: true });
    try {
      const updated = await updateSettingsRequest(makeSettingsUpdate(draft));
      if (!updated) {
        return { saved: false, previousApplied: null, updated: null };
      }
      applyPersistedDashboardSettings(updated);
      options.pushRuntimeEvent(i18next.t('status.settings_saved'), 'success');
      return { saved: true, previousApplied, updated };
    } catch (error) {
      reportWarn(options.pushRuntimeEvent, error, i18next.t('status.settings_save_failed'));
      return { saved: false, previousApplied: null, updated: null };
    } finally {
      stateRef.current = settingsReducer(stateRef.current, { type: 'set_saving', value: false });
      dispatch({ type: 'set_saving', value: false });
    }
  }

  async function saveSettingsChanges(): Promise<boolean> {
    const result = await saveDashboardSettings();
    return result.saved;
  }

  function setRoutingMode(nextMode: string): void {
    if (!routingModes.some((mode) => mode.id === nextMode)) return;
    applySingleSettingPatch('routingStrategy', nextMode);
  }

  function setScoringWeight(key: 'alpha' | 'beta' | 'gamma' | 'delta' | 'zeta' | 'eta', value: number): void {
    applySingleSettingPatch(scoringWeightKeyToField(key), clampUnitInterval(value));
  }

  function setHeadroomWeight(key: 'wp' | 'ws', value: number): void {
    applySingleSettingPatch(headroomWeightKeyToField(key), clampUnitInterval(value));
  }

  function setStrategyParam(modeId: string, key: string, value: number): void {
    void modeId;
    void key;
    void value;
  }

  function setTheme(theme: ThemeMode): void {
    applySingleSettingPatch('theme', theme);
  }

  function setUpstreamStreamTransport(transport: UpstreamStreamTransport): void {
    applySingleSettingPatch('upstreamStreamTransport', transport);
  }

  function setStickyThreadsEnabled(enabled: boolean): void {
    applySingleSettingPatch('stickyThreadsEnabled', enabled);
  }

  function setPreferEarlierResetAccounts(enabled: boolean): void {
    applySingleSettingPatch('preferEarlierResetAccounts', enabled);
  }

  function setStrictLockPoolContinuations(enabled: boolean): void {
    applySingleSettingPatch('strictLockPoolContinuations', enabled);
  }

  async function updateLockedRoutingAccountIds(accountIds: string[]): Promise<boolean> {
    const nextIds = Array.from(
      new Set(
        accountIds
          .map((value) => value.trim())
          .filter((value) => value.length > 0),
      ),
    );
    try {
      const currentDraft = stateRef.current.dashboardSettings;
      const updated = await updateSettingsRequest({ lockedRoutingAccountIds: nextIds });
      if (!updated) {
        return false;
      }
      applyAppliedDashboardSettings(updated);
      applyDraftDashboardSettings({
        ...currentDraft,
        lockedRoutingAccountIds: updated.lockedRoutingAccountIds ?? nextIds,
      });
      return true;
    } catch (error) {
      reportWarn(options.pushRuntimeEvent, error, i18next.t('status.locked_routing_pool_failed'));
      return false;
    }
  }

  function setOpenaiCacheAffinityMaxAgeSeconds(seconds: number): void {
    applySingleSettingPatch('openaiCacheAffinityMaxAgeSeconds', Math.max(1, seconds));
  }

  function setImportWithoutOverwrite(enabled: boolean): void {
    applySingleSettingPatch('importWithoutOverwrite', enabled);
  }

  function setRoutingPlanModelPricingUsdPerMillion(value: string): void {
    applySingleSettingPatch('routingPlanModelPricingUsdPerMillion', value.trim());
  }

  function setSyncClusterName(clusterName: string): void {
    applySingleSettingPatch('syncClusterName', normalizeSyncClusterName(clusterName));
  }

  function setSyncSiteId(siteId: number): void {
    applySingleSettingPatch('syncSiteId', boundedInteger(siteId, 1, Number.MAX_SAFE_INTEGER));
  }

  function setSyncPort(port: number): void {
    applySingleSettingPatch('syncPort', boundedInteger(port, 1, 65535));
  }

  function setSyncDiscoveryEnabled(enabled: boolean): void {
    applySingleSettingPatch('syncDiscoveryEnabled', enabled);
  }

  function setSyncIntervalSeconds(seconds: number): void {
    applySingleSettingPatch('syncIntervalSeconds', boundedInteger(seconds, 0, 86400));
  }

  function setSyncConflictResolution(strategy: SyncConflictResolution): void {
    applySingleSettingPatch('syncConflictResolution', strategy);
  }

  function setSyncJournalRetentionDays(days: number): void {
    applySingleSettingPatch('syncJournalRetentionDays', boundedInteger(days, 1, 3650));
  }

  function setSyncTlsEnabled(enabled: boolean): void {
    applySingleSettingPatch('syncTlsEnabled', enabled);
  }

  function setSyncRequireHandshakeAuth(enabled: boolean): void {
    applySingleSettingPatch('syncRequireHandshakeAuth', enabled);
  }

  function setSyncClusterSharedSecret(secret: string): void {
    applySingleSettingPatch('syncClusterSharedSecret', secret);
  }

  function setSyncTlsVerifyPeer(enabled: boolean): void {
    applySingleSettingPatch('syncTlsVerifyPeer', enabled);
  }

  function setSyncTlsCaCertificatePath(path: string): void {
    applySingleSettingPatch('syncTlsCaCertificatePath', path.trim());
  }

  function setSyncTlsCertificateChainPath(path: string): void {
    applySingleSettingPatch('syncTlsCertificateChainPath', path.trim());
  }

  function setSyncTlsPrivateKeyPath(path: string): void {
    applySingleSettingPatch('syncTlsPrivateKeyPath', path.trim());
  }

  function setSyncTlsPinnedPeerCertificateSha256(value: string): void {
    applySingleSettingPatch('syncTlsPinnedPeerCertificateSha256', value.trim());
  }

  function setSyncSchemaVersion(version: number): void {
    applyDashboardSettingsPatch(buildSyncSchemaVersionPatch(stateRef.current.dashboardSettings, version));
  }

  function setSyncMinSupportedSchemaVersion(version: number): void {
    applyDashboardSettingsPatch(buildSyncMinSupportedSchemaVersionPatch(stateRef.current.dashboardSettings, version));
  }

  function setSyncAllowSchemaDowngrade(enabled: boolean): void {
    applySingleSettingPatch('syncAllowSchemaDowngrade', enabled);
  }

  function setSyncPeerProbeEnabled(enabled: boolean): void {
    applySingleSettingPatch('syncPeerProbeEnabled', enabled);
  }

  function setSyncPeerProbeIntervalMs(value: number): void {
    applySingleSettingPatch('syncPeerProbeIntervalMs', boundedInteger(value, 100, 300_000));
  }

  function setSyncPeerProbeTimeoutMs(value: number): void {
    applySingleSettingPatch('syncPeerProbeTimeoutMs', boundedInteger(value, 50, 60_000));
  }

  function setSyncPeerProbeMaxPerRefresh(value: number): void {
    applySingleSettingPatch('syncPeerProbeMaxPerRefresh', boundedInteger(value, 1, 64));
  }

  function setSyncPeerProbeFailClosed(enabled: boolean): void {
    applySingleSettingPatch('syncPeerProbeFailClosed', enabled);
  }

  function setSyncPeerProbeFailClosedFailures(value: number): void {
    applySingleSettingPatch('syncPeerProbeFailClosedFailures', boundedInteger(value, 1, 1000));
  }

  return {
    routingModes,
    dashboardSettings,
    appliedDashboardSettings,
    settingsSaveInFlight,
    hasUnsavedSettingsChanges: !dashboardSettingsEqual(dashboardSettings, appliedDashboardSettings),
    applyPersistedDashboardSettings,
    refreshDashboardSettingsFromNative,
    discardDashboardSettingsChanges,
    saveDashboardSettings,
    saveSettingsChanges,
    setRoutingMode,
    setScoringWeight,
    setHeadroomWeight,
    setStrategyParam,
    setTheme,
    setUpstreamStreamTransport,
    setStickyThreadsEnabled,
    setPreferEarlierResetAccounts,
    setStrictLockPoolContinuations,
    updateLockedRoutingAccountIds,
    setOpenaiCacheAffinityMaxAgeSeconds,
    setImportWithoutOverwrite,
    setRoutingPlanModelPricingUsdPerMillion,
    setSyncClusterName,
    setSyncSiteId,
    setSyncPort,
    setSyncDiscoveryEnabled,
    setSyncIntervalSeconds,
    setSyncConflictResolution,
    setSyncJournalRetentionDays,
    setSyncTlsEnabled,
    setSyncRequireHandshakeAuth,
    setSyncClusterSharedSecret,
    setSyncTlsVerifyPeer,
    setSyncTlsCaCertificatePath,
    setSyncTlsCertificateChainPath,
    setSyncTlsPrivateKeyPath,
    setSyncTlsPinnedPeerCertificateSha256,
    setSyncSchemaVersion,
    setSyncMinSupportedSchemaVersion,
    setSyncAllowSchemaDowngrade,
    setSyncPeerProbeEnabled,
    setSyncPeerProbeIntervalMs,
    setSyncPeerProbeTimeoutMs,
    setSyncPeerProbeMaxPerRefresh,
    setSyncPeerProbeFailClosed,
    setSyncPeerProbeFailClosedFailures,
  };
}
