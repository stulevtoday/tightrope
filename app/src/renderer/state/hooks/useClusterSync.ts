import { useCallback, useEffect, useRef, useState } from 'react';
import i18next from 'i18next';
import type { TightropeService } from '../../services/tightrope';
import type {
  ClusterStatus,
  DashboardSettings,
  DashboardSettingsUpdate,
} from '../../shared/types';
import { reportWarn } from '../errors';
import type { StatusNoticeLevel } from '../statusNotices';
import {
  reconfigureSyncClusterFlow,
  toggleSyncEnabledFlow,
} from './useClusterSyncFlows';
import {
  canScheduleClusterAutoSync,
  reportClusterPollingFailureOnce,
  resetClusterPollingError,
  runClusterAutoSyncTick,
} from './useClusterSyncPolling';
import { emptyClusterStatus, normalizeClusterStatus } from './useClusterSyncStatus';
import { useMountedFlag } from './useMountedFlag';

const DEFAULT_CLUSTER_POLL_MS = 2000;

export interface UseClusterSyncOptions {
  dashboardSettings: DashboardSettings;
  appliedDashboardSettings: DashboardSettings;
  hasUnsavedSettingsChanges: boolean;
  makeSettingsUpdate: (settings: DashboardSettings) => DashboardSettingsUpdate;
  applyPersistedDashboardSettings: (nextSettings: DashboardSettings, preserveDraft?: boolean) => void;
  pushRuntimeEvent: (text: string, level?: StatusNoticeLevel) => void;
  getClusterStatusRequest: TightropeService['getClusterStatusRequest'];
  clusterEnableRequest: TightropeService['clusterEnableRequest'];
  clusterDisableRequest: TightropeService['clusterDisableRequest'];
  addPeerRequest: TightropeService['addPeerRequest'];
  removePeerRequest: TightropeService['removePeerRequest'];
  triggerSyncRequest: TightropeService['triggerSyncRequest'];
  updateSettingsRequest: TightropeService['updateSettingsRequest'];
}

interface UseClusterSyncConfig {
  clusterPollMs?: number;
}

interface UseClusterSyncResult {
  clusterStatus: ClusterStatus;
  manualPeerAddress: string;
  setManualPeer: (value: string) => void;
  refreshClusterState: () => Promise<void>;
  reconfigureSyncCluster: (settings: DashboardSettings) => Promise<void>;
  toggleSyncEnabled: () => Promise<void>;
  addManualPeer: () => Promise<void>;
  removeSyncPeer: (siteId: string) => Promise<void>;
  triggerSyncNow: () => Promise<void>;
}

export function useClusterSync(options: UseClusterSyncOptions, config: UseClusterSyncConfig = {}): UseClusterSyncResult {
  const {
    getClusterStatusRequest,
    clusterEnableRequest,
    clusterDisableRequest,
    addPeerRequest,
    removePeerRequest,
    triggerSyncRequest,
    updateSettingsRequest,
  } = options;
  const { clusterPollMs = DEFAULT_CLUSTER_POLL_MS } = config;
  const {
    dashboardSettings,
    appliedDashboardSettings,
    hasUnsavedSettingsChanges,
    makeSettingsUpdate,
    applyPersistedDashboardSettings,
    pushRuntimeEvent,
  } = options;
  const [clusterStatus, setClusterStatus] = useState<ClusterStatus>({ ...emptyClusterStatus });
  const [manualPeerAddress, setManualPeerAddress] = useState('');
  const mountedRef = useMountedFlag();
  const clusterPollErrorReportedRef = useRef(false);

  const refreshClusterState = useCallback(async (): Promise<void> => {
    const status = await getClusterStatusRequest();
    if (!status || !mountedRef.current) {
      return;
    }
    setClusterStatus(normalizeClusterStatus(status));
    resetClusterPollingError(clusterPollErrorReportedRef);
  }, [getClusterStatusRequest, mountedRef]);

  const canScheduleAutoSync = canScheduleClusterAutoSync(clusterStatus, appliedDashboardSettings);

  useEffect(() => {
    const handle = setInterval(() => {
      void refreshClusterState().catch(() => {
        reportClusterPollingFailureOnce(clusterPollErrorReportedRef, pushRuntimeEvent);
      });
    }, clusterPollMs);
    return () => clearInterval(handle);
  }, [clusterPollMs, pushRuntimeEvent, refreshClusterState]);

  useEffect(() => {
    if (!canScheduleAutoSync) {
      return;
    }
    const handle = setInterval(() => {
      void runClusterAutoSyncTick(triggerSyncRequest, refreshClusterState, pushRuntimeEvent);
    }, appliedDashboardSettings.syncIntervalSeconds * 1000);
    return () => clearInterval(handle);
  }, [appliedDashboardSettings.syncIntervalSeconds, canScheduleAutoSync, pushRuntimeEvent, refreshClusterState, triggerSyncRequest]);

  const reconfigureSyncCluster = useCallback(async (settings: DashboardSettings): Promise<void> => {
    await reconfigureSyncClusterFlow(
      {
        clusterEnabled: clusterStatus.enabled,
        clusterDisableRequest,
        clusterEnableRequest,
        refreshClusterState,
        pushRuntimeEvent,
      },
      settings,
    );
  }, [clusterDisableRequest, clusterEnableRequest, clusterStatus.enabled, pushRuntimeEvent, refreshClusterState]);

  const toggleSyncEnabled = useCallback(async (): Promise<void> => {
    await toggleSyncEnabledFlow({
      clusterEnabled: clusterStatus.enabled,
      hasUnsavedSettingsChanges,
      dashboardSettings,
      appliedDashboardSettings,
      makeSettingsUpdate,
      applyPersistedDashboardSettings,
      refreshClusterState,
      pushRuntimeEvent,
      clusterEnableRequest,
      clusterDisableRequest,
      updateSettingsRequest,
    });
  }, [
    applyPersistedDashboardSettings,
    appliedDashboardSettings,
    clusterStatus.enabled,
    dashboardSettings,
    hasUnsavedSettingsChanges,
    makeSettingsUpdate,
    pushRuntimeEvent,
    refreshClusterState,
    clusterDisableRequest,
    clusterEnableRequest,
    updateSettingsRequest,
  ]);

  const setManualPeer = useCallback((value: string): void => {
    setManualPeerAddress(value);
  }, []);

  const addManualPeer = useCallback(async (): Promise<void> => {
    const peer = manualPeerAddress.trim();
    if (!peer) {
      return;
    }
    try {
      const added = await addPeerRequest(peer);
      if (!added || !mountedRef.current) {
        return;
      }
      setManualPeerAddress('');
      await refreshClusterState();
      pushRuntimeEvent(i18next.t('status.sync_peer_added', { peer }), 'success');
    } catch (error) {
      reportWarn(pushRuntimeEvent, error, 'Failed to add sync peer');
    }
  }, [addPeerRequest, manualPeerAddress, mountedRef, pushRuntimeEvent, refreshClusterState]);

  const removeSyncPeer = useCallback(async (siteId: string): Promise<void> => {
    try {
      const removed = await removePeerRequest(siteId);
      if (!removed) {
        return;
      }
      await refreshClusterState();
      pushRuntimeEvent(i18next.t('status.sync_peer_removed', { siteId }), 'success');
    } catch (error) {
      reportWarn(pushRuntimeEvent, error, 'Failed to remove sync peer');
    }
  }, [pushRuntimeEvent, refreshClusterState, removePeerRequest]);

  const triggerSyncNow = useCallback(async (): Promise<void> => {
    try {
      const triggered = await triggerSyncRequest();
      if (!triggered) {
        return;
      }
      await refreshClusterState();
      pushRuntimeEvent(i18next.t('status.sync_triggered'), 'success');
    } catch (error) {
      reportWarn(pushRuntimeEvent, error, 'Failed to trigger sync');
    }
  }, [pushRuntimeEvent, refreshClusterState, triggerSyncRequest]);

  return {
    clusterStatus,
    manualPeerAddress,
    setManualPeer,
    refreshClusterState,
    reconfigureSyncCluster,
    toggleSyncEnabled,
    addManualPeer,
    removeSyncPeer,
    triggerSyncNow,
  };
}
