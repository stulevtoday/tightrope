import { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { AccountsPage } from './components/accounts/AccountsPage';
import { AddAccountDialog } from './components/dialogs/AddAccountDialog';
import { AboutDialog } from './components/dialogs/AboutDialog';
import { AuthDialog } from './components/dialogs/AuthDialog';
import { BackendDialog } from './components/dialogs/BackendDialog';
import { ConnectedSyncTopologyDialog } from './components/dialogs/SyncTopologyDialog';
import { UnsavedSettingsDialog } from './components/dialogs/UnsavedSettingsDialog';
import { RouterToolbar } from './components/layout/RouterToolbar';
import { NavRail } from './components/layout/NavRail';
import { StatusBar } from './components/layout/StatusBar';
import { TitleBar } from './components/layout/TitleBar';
import { LogsPage } from './components/logs/LogsPage';
import { RequestDrawer } from './components/logs/RequestDrawer';
import { RouterPage } from './components/router/RouterPage';
import { SessionsPage } from './components/sessions/SessionsPage';
import { SettingsPage } from './components/settings/SettingsPage';
import { AppStateProviders, useTightropeService } from './state/context';
import type { TightropeService } from './services/tightrope';
import type { AppMetaResponse } from './shared/types';

interface AppProps {
  service?: TightropeService;
}

function AppShell() {
  const { t } = useTranslation();
  const service = useTightropeService();
  const [aboutDialogOpen, setAboutDialogOpen] = useState(false);
  const [appMeta, setAppMeta] = useState<AppMetaResponse | null>(null);
  const platform = service.platformRequest();

  useEffect(() => {
    let cancelled = false;
    void service.getAppMetaRequest()
      .then((meta) => {
        if (!cancelled) {
          setAppMeta(meta);
        }
      })
      .catch(() => {
        if (!cancelled) {
          setAppMeta(null);
        }
      });
    return () => {
      cancelled = true;
    };
  }, [service]);

  useEffect(() => {
    const unsubscribe = service.onAboutOpenRequest(() => {
      setAboutDialogOpen(true);
    });
    return () => {
      unsubscribe?.();
    };
  }, [service]);

  return (
    <main className="window" aria-label={`${t('titlebar.app_name')} ${t('titlebar.app_subtitle')}`}>
      <TitleBar onOpenAbout={() => setAboutDialogOpen(true)} />
      <div className="app-shell">
        <NavRail />
        <section className="workspace">
          <RouterToolbar />

          <RouterPage />

          <AccountsPage />

          <SessionsPage />

          <LogsPage />

          <SettingsPage />
        </section>
      </div>

      <StatusBar />

      <RequestDrawer />

      <BackendDialog />

      <AuthDialog />

      <ConnectedSyncTopologyDialog />

      <UnsavedSettingsDialog />

      <AboutDialog
        open={aboutDialogOpen}
        platform={platform}
        appMeta={appMeta}
        onClose={() => setAboutDialogOpen(false)}
      />

      <AddAccountDialog />
    </main>
  );
}

export function App({ service }: AppProps) {
  return (
    <AppStateProviders service={service}>
      <AppShell />
    </AppStateProviders>
  );
}
