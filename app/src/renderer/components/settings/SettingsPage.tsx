import type {
  ClusterStatus,
  DashboardSettings,
  FirewallIpEntry,
  FirewallMode,
  RoutingMode,
  ScoringModel,
  SyncConflictResolution,
  ThemeMode,
  UpstreamStreamTransport,
} from '../../shared/types';
import { AppearanceSection } from './sections/AppearanceSection';
import { DatabaseSyncSection } from './sections/DatabaseSyncSection';
import { FirewallSection } from './sections/FirewallSection';
import { RoutingOptionsSection } from './sections/RoutingOptionsSection';
import { RoutingStrategySection } from './sections/RoutingStrategySection';

interface SettingsPageProps {
  visible: boolean;
  routingModes: RoutingMode[];
  routingMode: string;
  scoringModel: ScoringModel;
  theme: ThemeMode;
  dashboardSettings: DashboardSettings;
  firewallMode: FirewallMode;
  firewallEntries: FirewallIpEntry[];
  firewallDraftIpAddress: string;
  clusterStatus: ClusterStatus;
  manualPeerAddress: string;
  onSetRoutingMode: (modeId: string) => void;
  onSetStrategyParam: (modeId: string, key: string, value: number) => void;
  onSetScoringWeight: (key: 'alpha' | 'beta' | 'gamma' | 'delta' | 'zeta' | 'eta', value: number) => void;
  onSetHeadroomWeight: (key: 'wp' | 'ws', value: number) => void;
  onSetUpstreamStreamTransport: (transport: UpstreamStreamTransport) => void;
  onSetStickyThreadsEnabled: (enabled: boolean) => void;
  onSetPreferEarlierResetAccounts: (enabled: boolean) => void;
  onSetOpenaiCacheAffinityMaxAgeSeconds: (seconds: number) => void;
  onSetFirewallDraftIpAddress: (value: string) => void;
  onAddFirewallIpAddress: () => void;
  onRemoveFirewallIpAddress: (ipAddress: string) => void;
  onToggleSyncEnabled: () => void;
  onSetSyncSiteId: (siteId: number) => void;
  onSetSyncPort: (port: number) => void;
  onSetSyncDiscoveryEnabled: (enabled: boolean) => void;
  onSetSyncClusterName: (clusterName: string) => void;
  onSetManualPeerAddress: (value: string) => void;
  onAddManualPeer: () => void;
  onRemovePeer: (siteId: string) => void;
  onSetSyncIntervalSeconds: (seconds: number) => void;
  onSetSyncConflictResolution: (strategy: SyncConflictResolution) => void;
  onSetSyncJournalRetentionDays: (days: number) => void;
  onSetSyncTlsEnabled: (enabled: boolean) => void;
  onTriggerSyncNow: () => void;
  onSetTheme: (theme: ThemeMode) => void;
}

export function SettingsPage({
  visible,
  routingModes,
  routingMode,
  scoringModel,
  theme,
  dashboardSettings,
  firewallMode,
  firewallEntries,
  firewallDraftIpAddress,
  clusterStatus,
  manualPeerAddress,
  onSetRoutingMode,
  onSetStrategyParam,
  onSetScoringWeight,
  onSetHeadroomWeight,
  onSetUpstreamStreamTransport,
  onSetStickyThreadsEnabled,
  onSetPreferEarlierResetAccounts,
  onSetOpenaiCacheAffinityMaxAgeSeconds,
  onSetFirewallDraftIpAddress,
  onAddFirewallIpAddress,
  onRemoveFirewallIpAddress,
  onToggleSyncEnabled,
  onSetSyncSiteId,
  onSetSyncPort,
  onSetSyncDiscoveryEnabled,
  onSetSyncClusterName,
  onSetManualPeerAddress,
  onAddManualPeer,
  onRemovePeer,
  onSetSyncIntervalSeconds,
  onSetSyncConflictResolution,
  onSetSyncJournalRetentionDays,
  onSetSyncTlsEnabled,
  onTriggerSyncNow,
  onSetTheme,
}: SettingsPageProps) {
  if (!visible) return null;

  return (
    <section className="settings-page page active" id="pageSettings" data-page="settings">
      <div className="settings-scroll">
        <header className="section-header">
          <div>
            <p className="eyebrow">Configuration</p>
            <h2>Settings</h2>
          </div>
        </header>
        <div className="settings-body">
          <RoutingStrategySection
            routingModes={routingModes}
            routingMode={routingMode}
            scoringModel={scoringModel}
            onSetRoutingMode={onSetRoutingMode}
            onSetStrategyParam={onSetStrategyParam}
            onSetScoringWeight={onSetScoringWeight}
            onSetHeadroomWeight={onSetHeadroomWeight}
          />
          <RoutingOptionsSection
            upstreamStreamTransport={dashboardSettings.upstreamStreamTransport}
            stickyThreadsEnabled={dashboardSettings.stickyThreadsEnabled}
            preferEarlierResetAccounts={dashboardSettings.preferEarlierResetAccounts}
            openaiCacheAffinityMaxAgeSeconds={dashboardSettings.openaiCacheAffinityMaxAgeSeconds}
            onSetUpstreamStreamTransport={onSetUpstreamStreamTransport}
            onSetStickyThreadsEnabled={onSetStickyThreadsEnabled}
            onSetPreferEarlierResetAccounts={onSetPreferEarlierResetAccounts}
            onSetOpenaiCacheAffinityMaxAgeSeconds={onSetOpenaiCacheAffinityMaxAgeSeconds}
          />
          <FirewallSection
            mode={firewallMode}
            entries={firewallEntries}
            draftIpAddress={firewallDraftIpAddress}
            onSetDraftIpAddress={onSetFirewallDraftIpAddress}
            onAddIpAddress={onAddFirewallIpAddress}
            onRemoveIpAddress={onRemoveFirewallIpAddress}
          />
          <DatabaseSyncSection
            syncEnabled={clusterStatus.enabled}
            syncSiteId={dashboardSettings.syncSiteId}
            syncPort={dashboardSettings.syncPort}
            syncDiscoveryEnabled={dashboardSettings.syncDiscoveryEnabled}
            syncClusterName={dashboardSettings.syncClusterName}
            manualPeerAddress={manualPeerAddress}
            syncIntervalSeconds={dashboardSettings.syncIntervalSeconds}
            syncConflictResolution={dashboardSettings.syncConflictResolution}
            syncJournalRetentionDays={dashboardSettings.syncJournalRetentionDays}
            syncTlsEnabled={dashboardSettings.syncTlsEnabled}
            clusterStatus={clusterStatus}
            onToggleSyncEnabled={onToggleSyncEnabled}
            onSetSyncSiteId={onSetSyncSiteId}
            onSetSyncPort={onSetSyncPort}
            onSetSyncDiscoveryEnabled={onSetSyncDiscoveryEnabled}
            onSetSyncClusterName={onSetSyncClusterName}
            onSetManualPeerAddress={onSetManualPeerAddress}
            onAddManualPeer={onAddManualPeer}
            onRemovePeer={onRemovePeer}
            onSetSyncIntervalSeconds={onSetSyncIntervalSeconds}
            onSetSyncConflictResolution={onSetSyncConflictResolution}
            onSetSyncJournalRetentionDays={onSetSyncJournalRetentionDays}
            onSetSyncTlsEnabled={onSetSyncTlsEnabled}
            onTriggerSyncNow={onTriggerSyncNow}
          />
          <AppearanceSection theme={theme} onSetTheme={onSetTheme} />
        </div>
      </div>
    </section>
  );
}
