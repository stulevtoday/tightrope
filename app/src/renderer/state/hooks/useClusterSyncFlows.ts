import i18next from 'i18next';
import type { TightropeService } from '../../services/tightrope';
import type { DashboardSettings, DashboardSettingsUpdate } from '../../shared/types';
import { reportWarn } from '../errors';
import type { StatusNoticeLevel } from '../statusNotices';

type PushRuntimeEvent = (text: string, level?: StatusNoticeLevel) => void;

function isNonRoutableDiscoveryErrorMessage(message: string): boolean {
  const normalized = message.toLowerCase();
  return (
    normalized.includes('cluster discovery requires a routable host') ||
    normalized.includes('tightrope_host') ||
    normalized.includes('tightrope_connect_address')
  );
}

function isNonRoutableDiscoveryError(error: unknown): boolean {
  if (!(error instanceof Error)) {
    return false;
  }
  return isNonRoutableDiscoveryErrorMessage(error.message);
}

export function clusterConfigFromSettings(
  settings: DashboardSettings,
  manualPeers: string[] = [],
): {
  cluster_name: string;
  site_id: number;
  sync_port: number;
  discovery_enabled: boolean;
  conflict_resolution: DashboardSettings['syncConflictResolution'];
  journal_retention_days: number;
  require_handshake_auth: boolean;
  cluster_shared_secret: string;
  tls_enabled: boolean;
  tls_verify_peer: boolean;
  tls_ca_certificate_path: string;
  tls_certificate_chain_path: string;
  tls_private_key_path: string;
  tls_pinned_peer_certificate_sha256: string;
  sync_schema_version: number;
  min_supported_sync_schema_version: number;
  allow_schema_downgrade: boolean;
  peer_probe_enabled: boolean;
  peer_probe_interval_ms: number;
  peer_probe_timeout_ms: number;
  peer_probe_max_per_refresh: number;
  peer_probe_fail_closed: boolean;
  peer_probe_fail_closed_failures: number;
  manual_peers: string[];
} {
  return {
    cluster_name: settings.syncClusterName,
    site_id: settings.syncSiteId,
    sync_port: settings.syncPort,
    discovery_enabled: settings.syncDiscoveryEnabled,
    conflict_resolution: settings.syncConflictResolution,
    journal_retention_days: settings.syncJournalRetentionDays,
    require_handshake_auth: settings.syncRequireHandshakeAuth,
    cluster_shared_secret: settings.syncClusterSharedSecret,
    tls_enabled: settings.syncTlsEnabled,
    tls_verify_peer: settings.syncTlsVerifyPeer,
    tls_ca_certificate_path: settings.syncTlsCaCertificatePath,
    tls_certificate_chain_path: settings.syncTlsCertificateChainPath,
    tls_private_key_path: settings.syncTlsPrivateKeyPath,
    tls_pinned_peer_certificate_sha256: settings.syncTlsPinnedPeerCertificateSha256,
    sync_schema_version: settings.syncSchemaVersion,
    min_supported_sync_schema_version: settings.syncMinSupportedSchemaVersion,
    allow_schema_downgrade: settings.syncAllowSchemaDowngrade,
    peer_probe_enabled: settings.syncPeerProbeEnabled,
    peer_probe_interval_ms: settings.syncPeerProbeIntervalMs,
    peer_probe_timeout_ms: settings.syncPeerProbeTimeoutMs,
    peer_probe_max_per_refresh: settings.syncPeerProbeMaxPerRefresh,
    peer_probe_fail_closed: settings.syncPeerProbeFailClosed,
    peer_probe_fail_closed_failures: settings.syncPeerProbeFailClosedFailures,
    manual_peers: manualPeers,
  };
}

interface ReconfigureSyncClusterDeps {
  clusterEnabled: boolean;
  clusterDisableRequest: TightropeService['clusterDisableRequest'];
  clusterEnableRequest: TightropeService['clusterEnableRequest'];
  refreshClusterState: () => Promise<void>;
  pushRuntimeEvent: PushRuntimeEvent;
}

export async function reconfigureSyncClusterFlow(
  deps: ReconfigureSyncClusterDeps,
  settings: DashboardSettings,
): Promise<void> {
  if (!deps.clusterEnabled) {
    return;
  }
  try {
    const disabled = await deps.clusterDisableRequest();
    if (!disabled) {
      return;
    }
    const enabled = await deps.clusterEnableRequest(clusterConfigFromSettings(settings));
    if (!enabled) {
      return;
    }
    await deps.refreshClusterState();
    deps.pushRuntimeEvent('sync cluster reconfigured', 'success');
  } catch (error) {
    reportWarn(deps.pushRuntimeEvent, error, 'Failed to reconfigure sync cluster');
  }
}

interface ToggleSyncEnabledDeps {
  clusterEnabled: boolean;
  hasUnsavedSettingsChanges: boolean;
  dashboardSettings: DashboardSettings;
  appliedDashboardSettings: DashboardSettings;
  makeSettingsUpdate: (settings: DashboardSettings) => DashboardSettingsUpdate;
  applyPersistedDashboardSettings: (nextSettings: DashboardSettings, preserveDraft?: boolean) => void;
  refreshClusterState: () => Promise<void>;
  pushRuntimeEvent: PushRuntimeEvent;
  clusterEnableRequest: TightropeService['clusterEnableRequest'];
  clusterDisableRequest: TightropeService['clusterDisableRequest'];
  updateSettingsRequest: TightropeService['updateSettingsRequest'];
}

export async function toggleSyncEnabledFlow(deps: ToggleSyncEnabledDeps): Promise<void> {
  try {
    if (deps.clusterEnabled) {
      const disabled = await deps.clusterDisableRequest();
      if (!disabled) {
        return;
      }
      await deps.refreshClusterState();
      deps.pushRuntimeEvent(i18next.t('status.sync_cluster_disabled'), 'warn');
      return;
    }

    const settings = deps.hasUnsavedSettingsChanges ? deps.dashboardSettings : deps.appliedDashboardSettings;
    if (deps.hasUnsavedSettingsChanges) {
      deps.pushRuntimeEvent(i18next.t('status.sync_toggle_unsaved'), 'warn');
    }
    try {
      const enabled = await deps.clusterEnableRequest(clusterConfigFromSettings(settings));
      if (!enabled) {
        return;
      }
    } catch (error) {
      if (!settings.syncDiscoveryEnabled || !isNonRoutableDiscoveryError(error)) {
        throw error;
      }

      const fallbackSettings = {
        ...settings,
        syncDiscoveryEnabled: false,
      };
      const enabled = await deps.clusterEnableRequest(clusterConfigFromSettings(fallbackSettings));
      if (!enabled) {
        return;
      }
      const updated = await deps.updateSettingsRequest(deps.makeSettingsUpdate(fallbackSettings));
      if (updated) {
        deps.applyPersistedDashboardSettings(updated, deps.hasUnsavedSettingsChanges);
      }
      deps.pushRuntimeEvent(
        'sync cluster enabled with peer discovery disabled for localhost. Set TIGHTROPE_HOST or TIGHTROPE_CONNECT_ADDRESS to re-enable discovery.',
        'warn',
      );
      await deps.refreshClusterState();
      return;
    }
    await deps.refreshClusterState();
    deps.pushRuntimeEvent('sync cluster enabled', 'success');
  } catch (error) {
    reportWarn(deps.pushRuntimeEvent, error, 'Failed to toggle sync cluster');
  }
}
