import { useCallback, type Dispatch, type SetStateAction } from 'react';
import i18next from 'i18next';
import type { TightropeService } from '../../services/tightrope';
import type { AppRuntimeState } from '../../shared/types';
import { reportWarn } from '../errors';
import type { StatusNoticeLevel } from '../statusNotices';

export interface UseRuntimeStateOptions {
  runtimeState: AppRuntimeState['runtimeState'];
  setState: Dispatch<SetStateAction<AppRuntimeState>>;
  pushRuntimeEvent: (text: string, level?: StatusNoticeLevel) => void;
  backendStatusRequest: TightropeService['backendStatusRequest'];
  backendStartRequest: TightropeService['backendStartRequest'];
  backendStopRequest: TightropeService['backendStopRequest'];
}

interface UseRuntimeStateResult {
  refreshBackendState: () => Promise<void>;
  setRuntimeAction: (action: 'start' | 'restart' | 'stop') => void;
  toggleRoutePause: () => void;
  toggleAutoRestart: () => void;
}

function isBackendEnabled(response: { enabled: boolean } | null | undefined): boolean {
  return response?.enabled !== false;
}

export function useRuntimeState(options: UseRuntimeStateOptions): UseRuntimeStateResult {
  const { backendStatusRequest, backendStartRequest, backendStopRequest } = options;
  const { runtimeState, setState, pushRuntimeEvent } = options;

  const refreshBackendState = useCallback(async (): Promise<void> => {
    const status = await backendStatusRequest();
    const enabled = isBackendEnabled(status);
    setState((previous) => ({
      ...previous,
      runtimeState: {
        ...previous.runtimeState,
        backend: enabled ? 'running' : 'stopped',
        health: enabled ? 'ok' : 'warn',
        pausedRoutes: enabled ? previous.runtimeState.pausedRoutes : false,
      },
    }));
  }, [backendStatusRequest, setState]);

  const setRuntimeAction = useCallback((action: 'start' | 'restart' | 'stop'): void => {
    void (async () => {
      try {
        const response = action === 'stop' ? await backendStopRequest() : await backendStartRequest();
        const enabled = isBackendEnabled(response);
        setState((previous) => ({
          ...previous,
          runtimeState: {
            ...previous.runtimeState,
            backend: enabled ? 'running' : 'stopped',
            health: enabled ? 'ok' : 'warn',
            pausedRoutes: enabled ? (action === 'restart' ? false : previous.runtimeState.pausedRoutes) : false,
          },
        }));

        if (action === 'start') {
          pushRuntimeEvent(enabled ? i18next.t('status.backend_started') : i18next.t('status.backend_start_rejected'), enabled ? 'success' : 'warn');
          return;
        }
      if (action === 'restart') {
          pushRuntimeEvent(enabled ? i18next.t('status.backend_restarted') : i18next.t('status.backend_restart_rejected'), enabled ? 'success' : 'warn');
          return;
        }
        pushRuntimeEvent(enabled ? i18next.t('status.backend_stop_rejected') : i18next.t('status.backend_stopped'), 'warn');
      } catch (error) {
        reportWarn(pushRuntimeEvent, error, 'Failed to update backend state');
      }
    })();
  }, [backendStartRequest, backendStopRequest, pushRuntimeEvent, setState]);

  const toggleRoutePause = useCallback((): void => {
    if (runtimeState.backend !== 'running') {
      pushRuntimeEvent(i18next.t('status.pause_ignored_stopped'), 'warn');
      return;
    }

    const nextPaused = !runtimeState.pausedRoutes;
    setState((previous) => ({
      ...previous,
      runtimeState: {
        ...previous.runtimeState,
        pausedRoutes: nextPaused,
      },
    }));
    pushRuntimeEvent(nextPaused ? i18next.t('status.new_routes_paused') : i18next.t('status.new_routes_resumed'));
  }, [pushRuntimeEvent, runtimeState.backend, runtimeState.pausedRoutes, setState]);

  const toggleAutoRestart = useCallback((): void => {
    const nextAutoRestart = !runtimeState.autoRestart;
    setState((previous) => ({
      ...previous,
      runtimeState: {
        ...previous.runtimeState,
        autoRestart: nextAutoRestart,
      },
    }));
    pushRuntimeEvent(nextAutoRestart ? i18next.t('status.auto_restart_enabled') : i18next.t('status.auto_restart_disabled'));
  }, [pushRuntimeEvent, runtimeState.autoRestart, setState]);

  return {
    refreshBackendState,
    setRuntimeAction,
    toggleRoutePause,
    toggleAutoRestart,
  };
}
