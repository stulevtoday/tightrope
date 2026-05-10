import { useTranslation } from 'react-i18next';
import { useAccountsContext, useNavigationContext, useRouterDerivedContext, useRuntimeContext } from '../../state/context';

export function RouterToolbar() {
  const { t } = useTranslation();
  const navigation = useNavigationContext();
  const accounts = useAccountsContext();
  const derived = useRouterDerivedContext();
  const runtime = useRuntimeContext();
  const eligibleCount = Array.from(derived.metrics.values()).filter((metric) => metric.capability).length;

  if (navigation.currentPage !== 'router') return null;

  return (
    <header className="tool-strip">
      <div className="tool-context">
        <div className="router-state" data-state={derived.routerState}>
          <span className="state-dot" aria-hidden="true" />
          <div className="router-state-copy">
            <strong>{t('router.toolbar_router_state', { state: derived.routerState })}</strong>
            <span>
              {derived.routerState === 'stopped'
                ? t('router.toolbar_waiting_for_backend')
                : derived.routerState === 'paused'
                  ? t('router.toolbar_paused')
                  : derived.routerState === 'degraded'
                    ? t('router.toolbar_degraded')
                    : t('router.toolbar_healthy')}
            </span>
          </div>
        </div>
        <div className="tool-chip">
          <span>{t('router.toolbar_bind')}</span>
          <strong className="mono">{runtime.runtimeState.bind}</strong>
        </div>
        <div className="tool-chip">
          <span>{t('router.toolbar_strategy')}</span>
          <strong>{derived.modeLabel}</strong>
        </div>
        <div className="tool-chip">
          <span>{t('router.toolbar_eligible')}</span>
          <strong>{`${eligibleCount}/${accounts.accounts.length}`}</strong>
        </div>
      </div>
      <div className="tool-actions">
        <button className="tool-btn" id="startRouter" type="button" onClick={() => runtime.setRuntimeAction('start')}>
          {t('router.toolbar_start')}
        </button>
        <button className="tool-btn" id="restartRouter" type="button" onClick={() => runtime.setRuntimeAction('restart')}>
          {t('router.toolbar_restart')}
        </button>
        <button className="tool-btn accent" id="pauseRouter" type="button" onClick={runtime.toggleRoutePause}>
          {runtime.runtimeState.pausedRoutes ? t('router.toolbar_resume_routes') : t('router.toolbar_pause_routes')}
        </button>
        <span className="tool-sep" aria-hidden="true" />
        <button className="tool-btn" id="openBackendDialog" type="button" onClick={runtime.openBackendDialog}>
{t('router.toolbar_backend')}
        </button>
        <button className="tool-btn" id="openAuthDialog" type="button" onClick={runtime.openAuthDialog}>
          {t('router.toolbar_oauth')}
        </button>
      </div>
    </header>
  );
}
