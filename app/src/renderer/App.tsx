import { AccountsPage } from './components/accounts/AccountsPage';
import { AddAccountDialog } from './components/dialogs/AddAccountDialog';
import { AuthDialog } from './components/dialogs/AuthDialog';
import { BackendDialog } from './components/dialogs/BackendDialog';
import { RouterToolbar } from './components/layout/RouterToolbar';
import { NavRail } from './components/layout/NavRail';
import { StatusBar } from './components/layout/StatusBar';
import { TitleBar } from './components/layout/TitleBar';
import { LogsPage } from './components/logs/LogsPage';
import { RequestDrawer } from './components/logs/RequestDrawer';
import { RouterPage } from './components/router/RouterPage';
import { SessionsPage } from './components/sessions/SessionsPage';
import { SettingsPage } from './components/settings/SettingsPage';
import { useTightropeState } from './state/useTightropeState';

export function App() {
  const model = useTightropeState();
  const eligibleCount = Array.from(model.metrics.values()).filter((metric) => metric.capability).length;

  return (
    <main className="window" aria-label="tightrope routing workbench">
      <TitleBar />
      <div className="app-shell">
        <NavRail currentPage={model.state.currentPage} onSelectPage={model.setCurrentPage} />
        <section className="workspace">
          <RouterToolbar
            visible={model.state.currentPage === 'router'}
            routerState={model.routerState}
            strategyLabel={model.modeLabel}
            activeAccountsLabel={`${eligibleCount}/${model.accounts.length}`}
            bindLabel={model.state.runtimeState.bind}
            searchQuery={model.state.searchQuery}
            pauseLabel={model.state.runtimeState.pausedRoutes ? 'Resume Routes' : 'Pause Routes'}
            onSearch={model.setSearchQuery}
            onStart={() => model.setRuntimeAction('start')}
            onRestart={() => model.setRuntimeAction('restart')}
            onPause={model.toggleRoutePause}
            onOpenBackend={model.openBackendDialog}
            onOpenAuth={model.openAuthDialog}
          />

          <RouterPage
            visible={model.state.currentPage === 'router'}
            accounts={model.accounts}
            metrics={model.metrics}
            routedAccountId={model.routedAccountId}
            selectedAccountId={model.state.selectedAccountId}
            selectedRouteId={model.state.selectedRouteId}
            selectedRoute={model.selectedRoute}
            selectedRouteAccount={model.selectedRouteAccount}
            selectedMetric={model.selectedMetric}
            visibleRows={model.visibleRows}
            routingModes={model.routingModes}
            scoringModel={model.state.scoringModel}
            routingMode={model.state.routingMode}
            runtimeState={model.state.runtimeState}
            inspectorOpen={model.state.inspectorOpen}
            modeLabel={model.modeLabel}
            kpis={model.kpis}
            formatNumber={model.formatNumber}
            onSelectAccount={model.setSelectedAccountId}
            onSelectRoute={model.setSelectedRoute}
            onCloseInspector={() => model.setInspectorOpen(false)}
            onOpenAddAccount={model.openAddAccountDialog}
            onSetRoutingMode={model.setRoutingMode}
            onSetScoringWeight={model.setScoringWeight}
          />

          <AccountsPage
            visible={model.state.currentPage === 'accounts'}
            accounts={model.accounts}
            filteredAccounts={model.filteredAccounts}
            selectedAccountDetail={model.selectedAccountDetail}
            accountSearchQuery={model.state.accountSearchQuery}
            accountStatusFilter={model.state.accountStatusFilter}
            onOpenAddAccount={model.openAddAccountDialog}
            onSearch={model.setAccountSearchQuery}
            onFilterStatus={model.setAccountStatusFilter}
            onSelectDetail={model.selectAccountDetail}
            stableSparklinePercents={model.stableSparklinePercents}
            deterministicAccountDetailValues={model.deterministicAccountDetailValues}
            formatNumber={model.formatNumber}
            isRefreshingUsageTelemetry={model.isRefreshingSelectedAccountTelemetry}
            onRefreshUsageTelemetry={model.refreshSelectedAccountTelemetry}
            onPauseAccount={model.pauseSelectedAccount}
            onReactivateAccount={model.reactivateSelectedAccount}
            onDeleteAccount={model.deleteSelectedAccount}
          />

          <SessionsPage
            visible={model.state.currentPage === 'sessions'}
            accounts={model.accounts}
            sessionsKindFilter={model.state.sessionsKindFilter}
            sessionsView={model.sessionsView}
            paginationLabel={model.sessionsPaginationLabel}
            canPrev={model.canPrevSessions}
            canNext={model.canNextSessions}
            onSetKindFilter={model.setSessionsKindFilter}
            onPrevPage={model.prevSessionsPage}
            onNextPage={model.nextSessionsPage}
            onPurgeStale={model.purgeStaleSessions}
          />

          <LogsPage
            visible={model.state.currentPage === 'logs'}
            rows={model.state.rows}
            accounts={model.accounts}
            formatNumber={model.formatNumber}
            onOpenDrawer={model.openDrawer}
          />

          <SettingsPage
            visible={model.state.currentPage === 'settings'}
            routingModes={model.routingModes}
            routingMode={model.state.routingMode}
            scoringModel={model.state.scoringModel}
            theme={model.state.theme}
            dashboardSettings={model.dashboardSettings}
            firewallMode={model.firewallMode}
            firewallEntries={model.firewallEntries}
            firewallDraftIpAddress={model.firewallDraftIpAddress}
            clusterStatus={model.clusterStatus}
            manualPeerAddress={model.manualPeerAddress}
            onSetRoutingMode={model.setRoutingMode}
            onSetStrategyParam={model.setStrategyParam}
            onSetScoringWeight={model.setScoringWeight}
            onSetHeadroomWeight={model.setHeadroomWeight}
            onSetUpstreamStreamTransport={model.setUpstreamStreamTransport}
            onSetStickyThreadsEnabled={model.setStickyThreadsEnabled}
            onSetPreferEarlierResetAccounts={model.setPreferEarlierResetAccounts}
            onSetOpenaiCacheAffinityMaxAgeSeconds={model.setOpenaiCacheAffinityMaxAgeSeconds}
            onSetFirewallDraftIpAddress={model.setFirewallDraft}
            onAddFirewallIpAddress={model.addFirewallIpAddress}
            onRemoveFirewallIpAddress={model.removeFirewallIpAddress}
            onToggleSyncEnabled={model.toggleSyncEnabled}
            onSetSyncSiteId={model.setSyncSiteId}
            onSetSyncPort={model.setSyncPort}
            onSetSyncDiscoveryEnabled={model.setSyncDiscoveryEnabled}
            onSetSyncClusterName={model.setSyncClusterName}
            onSetManualPeerAddress={model.setManualPeer}
            onAddManualPeer={model.addManualPeer}
            onRemovePeer={model.removeSyncPeer}
            onSetSyncIntervalSeconds={model.setSyncIntervalSeconds}
            onSetSyncConflictResolution={model.setSyncConflictResolution}
            onSetSyncJournalRetentionDays={model.setSyncJournalRetentionDays}
            onSetSyncTlsEnabled={model.setSyncTlsEnabled}
            onTriggerSyncNow={model.triggerSyncNow}
            onSetTheme={model.setTheme}
          />
        </section>
      </div>

      <StatusBar />

      <RequestDrawer row={model.drawerRow} accounts={model.accounts} metrics={model.metrics} formatNumber={model.formatNumber} onClose={model.closeDrawer} />

      <BackendDialog
        open={model.state.backendDialogOpen}
        runtimeState={model.state.runtimeState}
        onClose={model.closeBackendDialog}
        onStart={() => model.setRuntimeAction('start')}
        onRestart={() => model.setRuntimeAction('restart')}
        onStop={() => model.setRuntimeAction('stop')}
        onToggleAutoRestart={model.toggleAutoRestart}
      />

      <AuthDialog
        open={model.state.authDialogOpen}
        authState={model.state.authState}
        onClose={model.closeAuthDialog}
        onInit={model.initAuth0}
        onGenerateUrl={model.createListenerUrl}
        onToggleListener={model.toggleListener}
        onRestartListener={model.restartListener}
        onCapture={model.captureAuthResponse}
      />

      <AddAccountDialog
        open={model.state.addAccountOpen}
        step={model.state.addAccountStep}
        selectedFileName={model.state.selectedFileName}
        manualCallback={model.state.manualCallback}
        browserAuthUrl={model.state.browserAuthUrl}
        deviceVerifyUrl={model.state.deviceVerifyUrl}
        deviceUserCode={model.state.deviceUserCode}
        deviceCountdownSeconds={model.state.deviceCountdownSeconds}
        copyAuthLabel={model.state.copyAuthLabel}
        copyDeviceLabel={model.state.copyDeviceLabel}
        successEmail={model.state.successEmail}
        successPlan={model.state.successPlan}
        errorMessage={model.state.addAccountError}
        onClose={model.closeAddAccountDialog}
        onSetStep={model.setAddAccountStep}
        onSelectFile={model.selectImportFile}
        onSubmitImport={model.submitImport}
        onStartBrowserFlow={model.simulateBrowserAuth}
        onSubmitManualCallback={model.submitManualCallback}
        onSetManualCallback={model.setManualCallback}
        onCopyAuthUrl={() => {
          void model.copyBrowserAuthUrl();
        }}
        onStartDeviceFlow={model.startDeviceFlow}
        onCancelDeviceFlow={model.cancelDeviceFlow}
        onCopyDeviceUrl={() => {
          void model.copyDeviceVerificationUrl();
        }}
        onOpenDeviceUrl={() => window.open(model.state.deviceVerifyUrl, '_blank', 'noopener,noreferrer')}
        onDoneSuccess={model.closeAddAccountDialog}
      />
    </main>
  );
}
