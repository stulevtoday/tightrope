import type { Dispatch, MutableRefObject, SetStateAction } from 'react';
import i18next from 'i18next';
import type { TightropeService } from '../../services/tightrope';
import type { AddAccountStep, AppRuntimeState } from '../../shared/types';
import { DEFAULT_DEVICE_EXPIRES_SECONDS } from './useOAuthFlowHelpers';
import {
  applyAddAccountErrorState,
  applyDeviceFlowCancelledState,
  applyDeviceFlowStartedState,
} from './useOAuthFlowStateTransitions';

interface SetAddAccountErrorOptions {
  syncFlowError?: boolean;
  reportRuntimeEvent?: boolean;
}

interface OAuthFlowDeviceActionDeps {
  state: AppRuntimeState;
  setState: Dispatch<SetStateAction<AppRuntimeState>>;
  oauthStartRequest: TightropeService['oauthStartRequest'];
  oauthStatusRequest: TightropeService['oauthStatusRequest'];
  oauthCompleteRequest: TightropeService['oauthCompleteRequest'];
  clearDeviceFlowTimers: () => void;
  captureOauthAccountBaseline: () => void;
  setFlowPhase: (phase: AddAccountStep) => void;
  setFlowError: (message: string) => void;
  setAddAccountErrorState: (message: string, optionsOverride?: SetAddAccountErrorOptions) => void;
  finalizeOauthAccountSuccess: (
    callbackUrl: string | null,
    fallbackHint: string,
    successEvent: string,
    successOptions?: { autoClose?: boolean; requireAccountVisible?: boolean },
  ) => Promise<void>;
  deviceTimerRef: MutableRefObject<ReturnType<typeof setInterval> | null>;
  oauthPollRef: MutableRefObject<ReturnType<typeof setInterval> | null>;
}

interface OAuthFlowDeviceActions {
  startDeviceFlow: () => Promise<void>;
  cancelDeviceFlow: () => void;
}

export function createOAuthFlowDeviceActions(deps: OAuthFlowDeviceActionDeps): OAuthFlowDeviceActions {
  async function startDeviceFlow(): Promise<void> {
    deps.clearDeviceFlowTimers();
    deps.captureOauthAccountBaseline();
    try {
      const start = await deps.oauthStartRequest('device');
      const expiresIn = start.expiresInSeconds ?? DEFAULT_DEVICE_EXPIRES_SECONDS;
      const intervalSeconds = start.intervalSeconds ?? 5;
      deps.setState((previous) => applyDeviceFlowStartedState(previous, start, expiresIn));
      deps.setFlowPhase('stepDevice');

      const completeBody = {
        deviceAuthId: start.deviceAuthId ?? undefined,
        userCode: start.userCode ?? undefined,
      };
      void deps.oauthCompleteRequest(completeBody).catch(() => {
        // completion polling failures are surfaced by status polling
      });

      deps.deviceTimerRef.current = setInterval(() => {
        deps.setState((previous) => {
          const next = previous.deviceCountdownSeconds - 1;
          if (next <= 0) {
            deps.clearDeviceFlowTimers();
            const expiredMessage = 'Device code expired. Please try again.';
            deps.setFlowError(expiredMessage);
            return applyAddAccountErrorState(
              {
                ...previous,
                deviceCountdownSeconds: 0,
              },
              expiredMessage,
            );
          }
          return {
            ...previous,
            deviceCountdownSeconds: next,
          };
        });
      }, 1000);

      deps.oauthPollRef.current = setInterval(() => {
        void deps.oauthStatusRequest()
          .then(async (status) => {
            if (status.status === 'success') {
              deps.clearDeviceFlowTimers();
              await deps.finalizeOauthAccountSuccess(
                status.callbackUrl ?? null,
                deps.state.deviceUserCode || `device-${Date.now().toString(36)}`,
                i18next.t('status.device_oauth_completed'),
              );
            } else if (status.status === 'error') {
              deps.clearDeviceFlowTimers();
              const message = status.errorMessage ?? i18next.t('status.device_oauth_failed');
              deps.setAddAccountErrorState(message);
            }
          })
          .catch(() => {
            // keep polling until timeout
          });
      }, Math.max(1000, intervalSeconds * 1000));
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Unable to start device oauth';
      deps.setAddAccountErrorState(message);
    }
  }

  function cancelDeviceFlow(): void {
    deps.clearDeviceFlowTimers();
    deps.setFlowPhase('stepMethod');
    deps.setState((previous) => applyDeviceFlowCancelledState(previous, DEFAULT_DEVICE_EXPIRES_SECONDS));
  }

  return {
    startDeviceFlow,
    cancelDeviceFlow,
  };
}
