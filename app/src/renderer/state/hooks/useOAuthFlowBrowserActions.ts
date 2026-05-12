import type { Dispatch, MutableRefObject, SetStateAction } from 'react';
import i18next from 'i18next';
import type { TightropeService } from '../../services/tightrope';
import type { AppRuntimeState, OauthStatusResponse } from '../../shared/types';
import { buildListenerUrl, nowStamp } from '../logic';
import {
  BROWSER_OAUTH_POLL_MS,
  DEFAULT_OAUTH_CALLBACK_PORT,
  OAUTH_DEEP_LINK_MAX_ATTEMPTS,
  OAUTH_DEEP_LINK_RETRY_MS,
  isValidOAuthAuthorizationUrl,
  oauthCallbackHasCode,
  oauthStateToken,
  sleep,
} from './useOAuthFlowHelpers';
import {
  applyCapturedCallbackState,
  applyOauthListenerErrorState,
  applyOauthListenerStartedState,
  applyOauthListenerStoppedState,
} from './useOAuthFlowStateTransitions';

interface SetAddAccountErrorOptions {
  syncFlowError?: boolean;
  reportRuntimeEvent?: boolean;
}

interface OAuthFlowBrowserActionDeps {
  state: AppRuntimeState;
  setState: Dispatch<SetStateAction<AppRuntimeState>>;
  pushRuntimeEvent: (text: string, level?: 'success' | 'warn') => void;
  oauthStartRequest: TightropeService['oauthStartRequest'];
  oauthStatusRequest: TightropeService['oauthStatusRequest'];
  oauthStopRequest: TightropeService['oauthStopRequest'];
  oauthRestartRequest: TightropeService['oauthRestartRequest'];
  browserOauthPollRef: MutableRefObject<ReturnType<typeof setInterval> | null>;
  oauthStartInFlightRef: MutableRefObject<Promise<string | null> | null>;
  oauthDeepLinkFinalizeInFlightRef: MutableRefObject<boolean>;
  stopBrowserOauthPolling: () => void;
  setAddAccountErrorState: (message: string, optionsOverride?: SetAddAccountErrorOptions) => void;
  applyOauthStatus: (status: OauthStatusResponse) => void;
  finalizeOauthAccountSuccess: (
    callbackUrl: string | null,
    fallbackHint: string,
    successEvent: string,
    successOptions?: { autoClose?: boolean; requireAccountVisible?: boolean },
  ) => Promise<void>;
}

interface OAuthFlowBrowserActions {
  completeBrowserOauthFromDeepLink: (deepLinkUrl: string) => Promise<void>;
  createListenerUrl: () => Promise<string | null>;
  startBrowserOauthPolling: () => void;
  bootstrapOauthState: () => Promise<void>;
  toggleListener: () => Promise<void>;
  restartListener: () => Promise<void>;
  initAuth0: () => Promise<void>;
  captureAuthResponse: () => Promise<void>;
}

export function createOAuthFlowBrowserActions(deps: OAuthFlowBrowserActionDeps): OAuthFlowBrowserActions {
  async function completeBrowserOauthFromDeepLink(deepLinkUrl: string): Promise<void> {
    if (deps.oauthDeepLinkFinalizeInFlightRef.current) {
      return;
    }
    deps.oauthDeepLinkFinalizeInFlightRef.current = true;
    deps.stopBrowserOauthPolling();
    try {
      let callbackUrl: string | null = null;
      let lastError: string | null = null;

      for (let attempt = 0; attempt < OAUTH_DEEP_LINK_MAX_ATTEMPTS; attempt += 1) {
        try {
          const status = await deps.oauthStatusRequest();
          deps.applyOauthStatus(status);
          callbackUrl = status.callbackUrl ?? callbackUrl;
          if (status.status === 'error') {
            throw new Error(status.errorMessage ?? i18next.t('dialogs.oauth_browser_failed'));
          }
          if (status.status === 'success') {
            await deps.finalizeOauthAccountSuccess(
              callbackUrl,
              deepLinkUrl,
              i18next.t('status.browser_oauth_completed'),
              { autoClose: true, requireAccountVisible: true },
            );
            return;
          }
        } catch (error) {
          lastError = error instanceof Error ? error.message : i18next.t('dialogs.oauth_verify_failed');
        }
        await sleep(OAUTH_DEEP_LINK_RETRY_MS);
      }

      const message = lastError ?? i18next.t('dialogs.oauth_callback_not_confirmed');
      deps.setAddAccountErrorState(message, { syncFlowError: false });
    } finally {
      deps.oauthDeepLinkFinalizeInFlightRef.current = false;
    }
  }

  async function createListenerUrl(): Promise<string | null> {
    if (deps.oauthStartInFlightRef.current) {
      return deps.oauthStartInFlightRef.current;
    }

    const pending = (async () => {
      try {
        const response = await deps.oauthStartRequest('browser');
        const callbackUrl = response.callbackUrl ?? buildListenerUrl(DEFAULT_OAUTH_CALLBACK_PORT, '/auth/callback');
        const authorizationUrl = isValidOAuthAuthorizationUrl(response.authorizationUrl)
          ? response.authorizationUrl
          : null;
        deps.setState((previous) => applyOauthListenerStartedState(previous, callbackUrl, authorizationUrl));
        deps.pushRuntimeEvent(i18next.t('status.callback_url_generated', { url: callbackUrl }));
        return authorizationUrl;
      } catch (error) {
        const message = error instanceof Error ? error.message : i18next.t('dialogs.oauth_start_failed');
        deps.setState((previous) => applyOauthListenerErrorState(previous, message));
        deps.pushRuntimeEvent(message, 'warn');
        return null;
      } finally {
        deps.oauthStartInFlightRef.current = null;
      }
    })();

    deps.oauthStartInFlightRef.current = pending;
    return pending;
  }

  async function pollBrowserOauthStatus(): Promise<void> {
    try {
      const status = await deps.oauthStatusRequest();
      if (!status || typeof status.status !== 'string') {
        return;
      }
      deps.applyOauthStatus(status);
      if (status.status === 'success') {
        if (!oauthCallbackHasCode(status.callbackUrl)) {
          return;
        }
        deps.stopBrowserOauthPolling();
        await deps.finalizeOauthAccountSuccess(
          status.callbackUrl ?? null,
          `oauth-${Date.now().toString(36)}`,
          i18next.t('status.browser_oauth_completed'),
         );
         return;
       }
       if (status.status === 'error') {
         deps.stopBrowserOauthPolling();
         const message = status.errorMessage ?? i18next.t('dialogs.oauth_browser_failed');
        deps.setAddAccountErrorState(message);
        return;
      }
      if (status.status === 'idle' || status.status === 'stopped') {
        deps.stopBrowserOauthPolling();
      }
    } catch {
      // keep polling while listener is active
    }
  }

  function startBrowserOauthPolling(): void {
    deps.stopBrowserOauthPolling();
    void pollBrowserOauthStatus();
    deps.browserOauthPollRef.current = setInterval(() => {
      void pollBrowserOauthStatus();
    }, BROWSER_OAUTH_POLL_MS);
  }

  async function bootstrapOauthState(): Promise<void> {
    const status = await deps.oauthStatusRequest();
    deps.applyOauthStatus(status);
    if (!isValidOAuthAuthorizationUrl(status.authorizationUrl)) {
      await createListenerUrl();
    }
  }

  async function toggleListener(): Promise<void> {
    if (!deps.state.authState.listenerRunning) {
      await createListenerUrl();
      return;
    }
    deps.stopBrowserOauthPolling();
    try {
      const status = await deps.oauthStopRequest();
      deps.applyOauthStatus(status);
    } catch {
      deps.setState((previous) => applyOauthListenerStoppedState(previous));
    }
    deps.pushRuntimeEvent(i18next.t('status.callback_listener_stopped'));
  }

  async function restartListener(): Promise<void> {
    deps.stopBrowserOauthPolling();
    try {
      const response = await deps.oauthRestartRequest();
      const callbackUrl = response.callbackUrl ?? buildListenerUrl(DEFAULT_OAUTH_CALLBACK_PORT, '/auth/callback');
      deps.setState((previous) => applyOauthListenerStartedState(previous, callbackUrl, response.authorizationUrl));
      deps.pushRuntimeEvent(i18next.t('status.callback_listener_restarted'), 'success');
    } catch (error) {
      const message = error instanceof Error ? error.message : i18next.t('dialogs.oauth_restart_listener_failed');
      deps.setState((previous) => applyOauthListenerErrorState(previous, message));
      deps.pushRuntimeEvent(message, 'warn');
    }
  }

  async function initAuth0(): Promise<void> {
    try {
      const status = await deps.oauthStatusRequest();
      deps.applyOauthStatus(status);
      deps.pushRuntimeEvent(i18next.t('status.oauth_status', { status: status.status }));
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to fetch OAuth status';
      deps.setState((previous) => applyOauthListenerErrorState(previous, message));
      deps.pushRuntimeEvent(message, 'warn');
    }
  }

  async function captureAuthResponse(): Promise<void> {
    const token = oauthStateToken(deps.state.browserAuthUrl);
    if (!token) {
      deps.pushRuntimeEvent('callback ignored: start listener first', 'warn');
      return;
    }

    const code = `code_${Math.random().toString(36).slice(2, 10)}`;
    try {
      await fetch(`/auth/callback?code=${encodeURIComponent(code)}&state=${encodeURIComponent(token)}`, { method: 'GET' });
      const status = await deps.oauthStatusRequest();
      deps.applyOauthStatus(status);
      deps.setState((previous) => applyCapturedCallbackState(previous, `${nowStamp()} ${code}`));
      deps.pushRuntimeEvent(`callback received (${code})`);
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to capture callback';
      deps.pushRuntimeEvent(message, 'warn');
    }
  }

  return {
    completeBrowserOauthFromDeepLink,
    createListenerUrl,
    startBrowserOauthPolling,
    bootstrapOauthState,
    toggleListener,
    restartListener,
    initAuth0,
    captureAuthResponse,
  };
}
