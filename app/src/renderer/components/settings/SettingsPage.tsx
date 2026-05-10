import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { AccountImportDialog } from '../dialogs/AccountImportDialog';
import { useNavigationContext, useSettingsContext } from '../../state/context';
import { AccountImportSection } from './sections/AccountImportSection';
import { AppearanceSection } from './sections/AppearanceSection';
import { DatabaseSecuritySection } from './sections/DatabaseSecuritySection';
import { DatabaseSyncSection } from './sections/DatabaseSyncSection';
import { FirewallSection } from './sections/FirewallSection';
import { RoutingOptionsSection } from './sections/RoutingOptionsSection';
import { RoutingStrategySection } from './sections/RoutingStrategySection';

export function SettingsPage() {
  const navigation = useNavigationContext();
  const settings = useSettingsContext();
  const { t } = useTranslation();
  const [accountImportDialogOpen, setAccountImportDialogOpen] = useState(false);

  if (navigation.currentPage !== 'settings') return null;

  return (
    <section className="settings-page page active" id="pageSettings" data-page="settings">
      <div className="settings-scroll">
        <header className="section-header">
          <div>
            <p className="eyebrow">{t('settings.eyebrow')}</p>
            <h2>{t('settings.title')}</h2>
          </div>
          <div className="settings-page-actions">
            <span className={`settings-dirty-pill${settings.settingsDirty ? ' active' : ''}`}>
              {settings.settingsDirty ? t('settings.unsaved_changes') : t('settings.all_changes_saved')}
            </span>
            <button
              className="dock-btn"
              type="button"
              disabled={!settings.settingsDirty || settings.settingsSaving}
              onClick={settings.discardSettings}
            >
              {t('settings.discard')}
            </button>
            <button
              className="dock-btn accent"
              type="button"
              disabled={!settings.settingsDirty || settings.settingsSaving}
              onClick={settings.saveSettings}
            >
              {settings.settingsSaving ? t('settings.saving') : t('settings.save')}
            </button>
          </div>
        </header>
        <div className="settings-body">
          <RoutingStrategySection
            routingModes={settings.routingModes}
            routingMode={settings.routingMode}
            scoringModel={settings.scoringModel}
            onSetRoutingMode={settings.setRoutingMode}
            onSetStrategyParam={settings.setStrategyParam}
            onSetScoringWeight={settings.setScoringWeight}
            onSetHeadroomWeight={settings.setHeadroomWeight}
          />
          <RoutingOptionsSection
            upstreamStreamTransport={settings.dashboardSettings.upstreamStreamTransport}
            stickyThreadsEnabled={settings.dashboardSettings.stickyThreadsEnabled}
            preferEarlierResetAccounts={settings.dashboardSettings.preferEarlierResetAccounts}
            strictLockPoolContinuations={settings.dashboardSettings.strictLockPoolContinuations}
            openaiCacheAffinityMaxAgeSeconds={settings.dashboardSettings.openaiCacheAffinityMaxAgeSeconds}
            onSetUpstreamStreamTransport={settings.setUpstreamStreamTransport}
            onSetStickyThreadsEnabled={settings.setStickyThreadsEnabled}
            onSetPreferEarlierResetAccounts={settings.setPreferEarlierResetAccounts}
            onSetStrictLockPoolContinuations={settings.setStrictLockPoolContinuations}
            onSetOpenaiCacheAffinityMaxAgeSeconds={settings.setOpenaiCacheAffinityMaxAgeSeconds}
          />
          <AccountImportSection
            importWithoutOverwrite={settings.dashboardSettings.importWithoutOverwrite}
            onSetImportWithoutOverwrite={settings.setImportWithoutOverwrite}
            onOpenImportDialog={() => setAccountImportDialogOpen(true)}
          />
          <FirewallSection
            mode={settings.firewallMode}
            entries={settings.firewallEntries}
            draftIpAddress={settings.firewallDraftIpAddress}
            onSetDraftIpAddress={settings.setFirewallDraftIpAddress}
            onAddIpAddress={settings.addFirewallIpAddress}
            onRemoveIpAddress={settings.removeFirewallIpAddress}
          />
          <DatabaseSecuritySection />
          <DatabaseSyncSection
            syncEnabled={settings.clusterStatus.enabled}
            syncSiteId={settings.dashboardSettings.syncSiteId}
            syncPort={settings.dashboardSettings.syncPort}
            syncDiscoveryEnabled={settings.dashboardSettings.syncDiscoveryEnabled}
            syncClusterName={settings.dashboardSettings.syncClusterName}
            manualPeerAddress={settings.manualPeerAddress}
            syncIntervalSeconds={settings.dashboardSettings.syncIntervalSeconds}
            syncConflictResolution={settings.dashboardSettings.syncConflictResolution}
            syncJournalRetentionDays={settings.dashboardSettings.syncJournalRetentionDays}
            syncTlsEnabled={settings.dashboardSettings.syncTlsEnabled}
            syncRequireHandshakeAuth={settings.dashboardSettings.syncRequireHandshakeAuth}
            syncClusterSharedSecret={settings.dashboardSettings.syncClusterSharedSecret}
            syncTlsVerifyPeer={settings.dashboardSettings.syncTlsVerifyPeer}
            syncTlsCaCertificatePath={settings.dashboardSettings.syncTlsCaCertificatePath}
            syncTlsCertificateChainPath={settings.dashboardSettings.syncTlsCertificateChainPath}
            syncTlsPrivateKeyPath={settings.dashboardSettings.syncTlsPrivateKeyPath}
            syncTlsPinnedPeerCertificateSha256={settings.dashboardSettings.syncTlsPinnedPeerCertificateSha256}
            syncSchemaVersion={settings.dashboardSettings.syncSchemaVersion}
            syncMinSupportedSchemaVersion={settings.dashboardSettings.syncMinSupportedSchemaVersion}
            syncAllowSchemaDowngrade={settings.dashboardSettings.syncAllowSchemaDowngrade}
            syncPeerProbeEnabled={settings.dashboardSettings.syncPeerProbeEnabled}
            syncPeerProbeIntervalMs={settings.dashboardSettings.syncPeerProbeIntervalMs}
            syncPeerProbeTimeoutMs={settings.dashboardSettings.syncPeerProbeTimeoutMs}
            syncPeerProbeMaxPerRefresh={settings.dashboardSettings.syncPeerProbeMaxPerRefresh}
            syncPeerProbeFailClosed={settings.dashboardSettings.syncPeerProbeFailClosed}
            syncPeerProbeFailClosedFailures={settings.dashboardSettings.syncPeerProbeFailClosedFailures}
            clusterStatus={settings.clusterStatus}
            onToggleSyncEnabled={settings.toggleSyncEnabled}
            onSetSyncSiteId={settings.setSyncSiteId}
            onSetSyncPort={settings.setSyncPort}
            onSetSyncDiscoveryEnabled={settings.setSyncDiscoveryEnabled}
            onSetSyncClusterName={settings.setSyncClusterName}
            onSetManualPeerAddress={settings.setManualPeerAddress}
            onAddManualPeer={settings.addManualPeer}
            onRemovePeer={settings.removePeer}
            onSetSyncIntervalSeconds={settings.setSyncIntervalSeconds}
            onSetSyncConflictResolution={settings.setSyncConflictResolution}
            onSetSyncJournalRetentionDays={settings.setSyncJournalRetentionDays}
            onSetSyncTlsEnabled={settings.setSyncTlsEnabled}
            onSetSyncRequireHandshakeAuth={settings.setSyncRequireHandshakeAuth}
            onSetSyncClusterSharedSecret={settings.setSyncClusterSharedSecret}
            onSetSyncTlsVerifyPeer={settings.setSyncTlsVerifyPeer}
            onSetSyncTlsCaCertificatePath={settings.setSyncTlsCaCertificatePath}
            onSetSyncTlsCertificateChainPath={settings.setSyncTlsCertificateChainPath}
            onSetSyncTlsPrivateKeyPath={settings.setSyncTlsPrivateKeyPath}
            onSetSyncTlsPinnedPeerCertificateSha256={settings.setSyncTlsPinnedPeerCertificateSha256}
            onSetSyncSchemaVersion={settings.setSyncSchemaVersion}
            onSetSyncMinSupportedSchemaVersion={settings.setSyncMinSupportedSchemaVersion}
            onSetSyncAllowSchemaDowngrade={settings.setSyncAllowSchemaDowngrade}
            onSetSyncPeerProbeEnabled={settings.setSyncPeerProbeEnabled}
            onSetSyncPeerProbeIntervalMs={settings.setSyncPeerProbeIntervalMs}
            onSetSyncPeerProbeTimeoutMs={settings.setSyncPeerProbeTimeoutMs}
            onSetSyncPeerProbeMaxPerRefresh={settings.setSyncPeerProbeMaxPerRefresh}
            onSetSyncPeerProbeFailClosed={settings.setSyncPeerProbeFailClosed}
            onSetSyncPeerProbeFailClosedFailures={settings.setSyncPeerProbeFailClosedFailures}
            onTriggerSyncNow={settings.triggerSyncNow}
            onOpenSyncTopology={settings.openSyncTopology}
          />
          <AppearanceSection theme={settings.theme} onSetTheme={settings.setTheme} />
        </div>
      </div>
      <AccountImportDialog
        open={accountImportDialogOpen}
        importWithoutOverwrite={settings.dashboardSettings.importWithoutOverwrite}
        onClose={() => setAccountImportDialogOpen(false)}
      />
    </section>
  );
}
