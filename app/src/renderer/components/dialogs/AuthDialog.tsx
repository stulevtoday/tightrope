import { useTranslation } from 'react-i18next';
import { useRuntimeContext } from '../../state/context';

export function AuthDialog() {
  const { t } = useTranslation();
  const runtime = useRuntimeContext();

  if (!runtime.authDialogOpen) return null;
  const authState = runtime.authState;

  return (
    <dialog open id="authDialog" onClick={(event) => event.currentTarget === event.target && runtime.closeAuthDialog()}>
      <header className="dialog-header">
        <h3>{t('dialogs.auth_title')}</h3>
        <button className="dialog-close" type="button" aria-label={t('common.close')} onClick={runtime.closeAuthDialog}>
          &times;
        </button>
      </header>
      <div className="dialog-body">
        <div className="summary-grid">
          <div>
            <span>{t('dialogs.auth_setup')}</span>
            <strong>{authState.initStatus}</strong>
          </div>
          <div>
            <span>{t('dialogs.auth_listener')}</span>
            <strong>{authState.listenerRunning ? 'running' : 'stopped'}</strong>
          </div>
          <div>
            <span>{t('dialogs.auth_callback')}</span>
            <strong className="mono">{authState.listenerUrl}</strong>
          </div>
          <div>
            <span>{t('dialogs.auth_last_callback')}</span>
            <strong>{authState.lastResponse}</strong>
          </div>
        </div>
        <div className="button-row">
          <button className="dock-btn" type="button" onClick={runtime.initAuth0}>
            {t('dialogs.auth_initialize')}
          </button>
          <button className="dock-btn" type="button" onClick={() => void runtime.createListenerUrl()}>
            {t('dialogs.auth_generate_url')}
          </button>
          <button className="dock-btn" type="button" onClick={() => void runtime.toggleListener()}>
            {authState.listenerRunning ? t('dialogs.auth_stop_listener') : t('dialogs.auth_start_listener')}
          </button>
          <button className="dock-btn" type="button" onClick={() => void runtime.restartListener()}>
            {t('dialogs.auth_restart_listener')}
          </button>
          <button className="dock-btn accent" type="button" onClick={() => void runtime.captureAuthResponse()}>
            {t('dialogs.auth_simulate_callback')}
          </button>
        </div>
        <div className="footnote">
          {t('dialogs.auth_footnote')} <span className="mono">/api/oauth/start</span>,{' '}
          <span className="mono">/api/oauth/stop</span>, <span className="mono">/api/oauth/restart</span>,{' '}
          <span className="mono">/api/oauth/status</span>, and <span className="mono">/auth/callback</span>.
        </div>
      </div>
    </dialog>
  );
}
