import { useTranslation } from 'react-i18next';
import { useRuntimeContext } from '../../state/context';
import { currentRouterState } from '../../state/logic';

export function BackendDialog() {
  const { t } = useTranslation();
  const runtime = useRuntimeContext();

  if (!runtime.backendDialogOpen) return null;
  const runtimeState = runtime.runtimeState;
  const routerState = currentRouterState(runtimeState);

  return (
    <dialog open id="backendDialog" onClick={(event) => event.currentTarget === event.target && runtime.closeBackendDialog()}>
      <header className="dialog-header">
        <h3>{t('dialogs.backend_title')}</h3>
        <button className="dialog-close" type="button" aria-label="Close" onClick={runtime.closeBackendDialog}>
          &times;
        </button>
      </header>
      <div className="dialog-body">
        <div className="summary-grid">
          <div>
            <span>{t('dialogs.backend_router')}</span>
            <strong>{routerState}</strong>
          </div>
          <div>
            <span>{t('dialogs.backend_backend')}</span>
            <strong>{runtimeState.backend}</strong>
          </div>
          <div>
            <span>{t('dialogs.backend_health')}</span>
            <strong>{runtimeState.health}</strong>
          </div>
          <div>
            <span>{t('dialogs.backend_restart')}</span>
            <strong>{runtimeState.autoRestart ? t('dialogs.backend_restart_armed') : t('dialogs.backend_restart_manual')}</strong>
          </div>
        </div>
        <div className="button-row">
          <button className="dock-btn" type="button" onClick={() => runtime.setRuntimeAction('start')}>
            {t('dialogs.backend_start')}
          </button>
          <button className="dock-btn" type="button" onClick={() => runtime.setRuntimeAction('restart')}>
            {t('dialogs.backend_restart_button')}
          </button>
          <button className="dock-btn" type="button" onClick={() => runtime.setRuntimeAction('stop')}>
            {t('dialogs.backend_stop')}
          </button>
          <button className="dock-btn accent" type="button" onClick={runtime.toggleAutoRestart}>
            {runtimeState.autoRestart ? t('dialogs.backend_disable_auto_restart') : t('dialogs.backend_enable_auto_restart')}
          </button>
        </div>
        <div className="footnote">
          <span className="mono">{runtimeState.bind}</span> loopback • {runtimeState.pausedRoutes ? t('dialogs.backend_loopback_paused') : t('dialogs.backend_loopback_accepting')}
        </div>
      </div>
    </dialog>
  );
}
