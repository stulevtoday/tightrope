import { useEffect, useReducer, useRef, type Dispatch, type SetStateAction } from 'react';
import i18next from 'i18next';
import type { TightropeService } from '../../services/tightrope';
import type {
  Account,
  AddAccountStep,
  AppRuntimeState,
  OauthDeepLinkEvent,
  OauthStatusResponse,
  RuntimeAccount,
} from '../../shared/types';
import type { StatusNoticeLevel } from '../statusNotices';
import {
  extractImportAccountPayload,
  isValidOAuthAuthorizationUrl,
  oauthEmailFromHints,
  readFileText,
  resetAddAccountTransientState,
  writeClipboardText,
} from './useOAuthFlowHelpers';
import { createInitialOAuthFlowReducerState, oauthFlowReducer } from './useOAuthFlowReducer';
import { createOAuthFlowBrowserActions } from './useOAuthFlowBrowserActions';
import { createOAuthFlowDeviceActions } from './useOAuthFlowDeviceActions';
import { useMountedFlag } from './useMountedFlag';
import {
  applyAddAccountErrorState,
  applyAddAccountStepState,
  applyAddAccountSuccessState,
  applyManualCallbackState,
  applyOauthStatusState,
  applySelectedFileNameState,
} from './useOAuthFlowStateTransitions';

export interface UseOAuthFlowOptions {
  state: AppRuntimeState;
  accounts: Account[];
  setState: Dispatch<SetStateAction<AppRuntimeState>>;
  refreshAccountsFromNative: () => Promise<RuntimeAccount[]>;
  refreshUsageTelemetryAfterAccountAdd: (accountId: string, accountName: string) => Promise<void>;
  pushRuntimeEvent: (text: string, level?: StatusNoticeLevel) => void;
  oauthStartRequest: TightropeService['oauthStartRequest'];
  oauthStatusRequest: TightropeService['oauthStatusRequest'];
  oauthStopRequest: TightropeService['oauthStopRequest'];
  oauthRestartRequest: TightropeService['oauthRestartRequest'];
  oauthCompleteRequest: TightropeService['oauthCompleteRequest'];
  oauthManualCallbackRequest: TightropeService['oauthManualCallbackRequest'];
  importAccountRequest: TightropeService['importAccountRequest'];
  onOauthDeepLinkRequest: TightropeService['onOauthDeepLinkRequest'];
}

interface UseOAuthFlowResult {
  bootstrapOauthState: () => Promise<void>;
  createListenerUrl: () => Promise<string | null>;
  toggleListener: () => Promise<void>;
  restartListener: () => Promise<void>;
  initAuth0: () => Promise<void>;
  captureAuthResponse: () => Promise<void>;
  openAddAccountDialog: () => void;
  closeAddAccountDialog: () => void;
  setAddAccountStep: (step: AddAccountStep) => void;
  selectImportFile: (file: File) => void;
  submitImport: () => Promise<void>;
  simulateBrowserAuth: () => Promise<void>;
  submitManualCallback: () => Promise<void>;
  setManualCallback: (value: string) => void;
  copyBrowserAuthUrl: () => Promise<void>;
  copyDeviceVerificationUrl: () => Promise<void>;
  startDeviceFlow: () => Promise<void>;
  cancelDeviceFlow: () => void;
  showAddAccountError: (message: string) => void;
}

export function useOAuthFlow(options: UseOAuthFlowOptions): UseOAuthFlowResult {
  const {
    oauthStartRequest,
    oauthStatusRequest,
    oauthStopRequest,
    oauthRestartRequest,
    oauthCompleteRequest,
    oauthManualCallbackRequest,
    importAccountRequest,
    onOauthDeepLinkRequest,
  } = options;
  const [flowState, dispatchFlow] = useReducer(oauthFlowReducer, createInitialOAuthFlowReducerState(options.state));
  const mountedRef = useMountedFlag();
  const deviceTimerRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const deviceSuccessRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const oauthPollRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const browserOauthPollRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const oauthStartInFlightRef = useRef<Promise<string | null> | null>(null);
  const oauthDeepLinkFinalizeInFlightRef = useRef(false);
  const oauthAccountBaselineIdsRef = useRef<Set<string>>(new Set());
  const copyTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const selectedImportFileRef = useRef<File | null>(null);
  const deepLinkHandlerRef = useRef<(event: OauthDeepLinkEvent) => void>(() => {});
  const previousAccountIdsRef = useRef<Set<string>>(new Set(options.accounts.map((account) => account.id)));

  function setRuntimeState(update: SetStateAction<AppRuntimeState>): void {
    if (!mountedRef.current) {
      return;
    }
    options.setState(update);
  }

  function setFlowPhase(phase: AddAccountStep): void {
    if (!mountedRef.current) {
      return;
    }
    dispatchFlow({ type: 'set_phase', phase });
  }

  function setFlowError(message: string): void {
    if (!mountedRef.current) {
      return;
    }
    dispatchFlow({ type: 'fail', message });
  }

  function resetFlowState(): void {
    if (!mountedRef.current) {
      return;
    }
    dispatchFlow({ type: 'reset' });
  }

  function setAddAccountErrorState(
    message: string,
    optionsOverride?: { syncFlowError?: boolean; reportRuntimeEvent?: boolean },
  ): void {
    const syncFlowError = optionsOverride?.syncFlowError ?? true;
    const reportRuntimeEvent = optionsOverride?.reportRuntimeEvent ?? true;
    if (syncFlowError) {
      setFlowError(message);
    }
    setRuntimeState((previous) => applyAddAccountErrorState(previous, message));
    if (reportRuntimeEvent) {
      options.pushRuntimeEvent(message, 'warn');
    }
  }

  function stopBrowserOauthPolling(): void {
    if (browserOauthPollRef.current) {
      clearInterval(browserOauthPollRef.current);
      browserOauthPollRef.current = null;
    }
  }

  function clearDeviceFlowTimers(): void {
    if (deviceTimerRef.current) clearInterval(deviceTimerRef.current);
    if (deviceSuccessRef.current) clearTimeout(deviceSuccessRef.current);
    if (oauthPollRef.current) clearInterval(oauthPollRef.current);
    deviceTimerRef.current = null;
    deviceSuccessRef.current = null;
    oauthPollRef.current = null;
  }

  function captureOauthAccountBaseline(): void {
    oauthAccountBaselineIdsRef.current = new Set(options.accounts.map((account) => account.id));
  }

  function applyOauthStatus(status: OauthStatusResponse): void {
    setRuntimeState((previous) => applyOauthStatusState(previous, status));
  }

  function selectOauthImportedAccount(
    runtimeAccounts: RuntimeAccount[],
    callbackUrl: string | null,
    fallbackHint: string,
  ): RuntimeAccount | null {
    if (runtimeAccounts.length === 0) {
      return null;
    }

    const baselineIds = oauthAccountBaselineIdsRef.current;
    const added = runtimeAccounts.find((record) => !baselineIds.has(record.accountId));
    if (added) {
      return added;
    }

    const hintedEmail = oauthEmailFromHints(callbackUrl, fallbackHint).toLowerCase();
    const hinted = runtimeAccounts.find((record) => record.email.toLowerCase() === hintedEmail);
    if (hinted) {
      return hinted;
    }

    const active = runtimeAccounts.find((record) => record.status === 'active');
    return active ?? runtimeAccounts[0];
  }

  async function finalizeOauthAccountSuccess(
    callbackUrl: string | null,
    fallbackHint: string,
    successEvent: string,
    successOptions?: { autoClose?: boolean; requireAccountVisible?: boolean },
  ): Promise<void> {
    try {
      const runtimeAccounts = await options.refreshAccountsFromNative();
      if (successOptions?.requireAccountVisible && runtimeAccounts.length === 0) {
        throw new Error('OAuth completed, but no account is visible yet. Please retry.');
      }
      const selected = selectOauthImportedAccount(runtimeAccounts, callbackUrl, fallbackHint);
      const fallbackEmail = oauthEmailFromHints(callbackUrl, fallbackHint);
      if (successOptions?.autoClose) {
        resetFlowState();
        setRuntimeState((previous) => resetAddAccountTransientState({ ...previous, addAccountOpen: false }));
      } else {
        setFlowPhase('stepSuccess');
        setRuntimeState((previous) =>
          applyAddAccountSuccessState(previous, {
            email: selected?.email ?? fallbackEmail,
            plan: selected?.provider || 'openai',
            selectedAccountDetailId: selected?.accountId,
          }),
        );
      }
      options.pushRuntimeEvent(successEvent, 'success');
      if (selected) {
        void options.refreshUsageTelemetryAfterAccountAdd(selected.accountId, selected.email);
      }
    } catch (error) {
      const message = error instanceof Error ? error.message : i18next.t('status.oauth_succeeded_refresh_failed');
      setAddAccountErrorState(message);
    }
  }

  const {
    completeBrowserOauthFromDeepLink,
    createListenerUrl,
    startBrowserOauthPolling,
    bootstrapOauthState,
    toggleListener,
    restartListener,
    initAuth0,
    captureAuthResponse,
  } = createOAuthFlowBrowserActions({
    state: options.state,
    setState: setRuntimeState,
    pushRuntimeEvent: options.pushRuntimeEvent,
    oauthStartRequest,
    oauthStatusRequest,
    oauthStopRequest,
    oauthRestartRequest,
    browserOauthPollRef,
    oauthStartInFlightRef,
    oauthDeepLinkFinalizeInFlightRef,
    stopBrowserOauthPolling,
    setAddAccountErrorState,
    applyOauthStatus,
    finalizeOauthAccountSuccess,
  });

  function openAddAccountDialog(): void {
    stopBrowserOauthPolling();
    selectedImportFileRef.current = null;
    captureOauthAccountBaseline();
    resetFlowState();
    setRuntimeState((previous) => resetAddAccountTransientState({ ...previous, addAccountOpen: true }));
  }

  function closeAddAccountDialog(): void {
    stopBrowserOauthPolling();
    clearDeviceFlowTimers();
    selectedImportFileRef.current = null;
    resetFlowState();
    setRuntimeState((previous) => resetAddAccountTransientState({ ...previous, addAccountOpen: false }));
  }

  useEffect(() => {
    const previousIds = previousAccountIdsRef.current;
    const nextIds = new Set(options.accounts.map((account) => account.id));
    const hasAddedAccount = options.accounts.some((account) => !previousIds.has(account.id));
    previousAccountIdsRef.current = nextIds;

    const waitingForOauth = options.state.addAccountOpen
      && (options.state.addAccountStep === 'stepBrowser' || options.state.addAccountStep === 'stepDevice');
    if (!waitingForOauth || !hasAddedAccount) {
      return;
    }

    closeAddAccountDialog();
    options.pushRuntimeEvent(i18next.t('status.oauth_dialog_closed_after_import'), 'success');
  }, [options.accounts, options.state.addAccountOpen, options.state.addAccountStep]);

  function setAddAccountStep(step: AddAccountStep): void {
    if (step !== 'stepBrowser') {
      stopBrowserOauthPolling();
    }
    setFlowPhase(step);
    setRuntimeState((previous) => applyAddAccountStepState(previous, step));
  }

  function selectImportFile(file: File): void {
    selectedImportFileRef.current = file;
    setRuntimeState((previous) => applySelectedFileNameState(previous, file.name));
  }

  async function submitImport(): Promise<void> {
    const file = selectedImportFileRef.current;
    if (!file) return;
    try {
      const payload = extractImportAccountPayload(JSON.parse(await readFileText(file)));
      if (!payload) {
        throw new Error(i18next.t('dialogs.add_account_import_missing_email'));
      }
      const imported = await importAccountRequest(payload.email, payload.provider, payload.access_token, payload.refresh_token);
      await options.refreshAccountsFromNative();
      setFlowPhase('stepSuccess');
      setRuntimeState((previous) =>
        applyAddAccountSuccessState(previous, {
          email: imported.email,
          plan: imported.provider || 'openai',
          selectedAccountDetailId: imported.accountId,
        }),
      );
      options.pushRuntimeEvent(i18next.t('status.account_imported', { email: imported.email }), 'success');
      void options.refreshUsageTelemetryAfterAccountAdd(imported.accountId, imported.email);
    } catch (error) {
      const message = error instanceof Error ? error.message : i18next.t('dialogs.add_account_import_failed');
      setAddAccountErrorState(message);
    }
  }

  async function simulateBrowserAuth(): Promise<void> {
    if (flowState.phase !== 'stepBrowser') {
      setFlowPhase('stepBrowser');
      setRuntimeState((previous) => applyAddAccountStepState(previous, 'stepBrowser'));
    }
    captureOauthAccountBaseline();
    const authorizationUrl = await createListenerUrl();
    if (authorizationUrl) {
      startBrowserOauthPolling();
      window.open(authorizationUrl, '_blank', 'noopener,noreferrer');
      return;
    }
    options.pushRuntimeEvent(i18next.t('status.browser_auth_url_not_ready'), 'warn');
  }

  async function submitManualCallback(): Promise<void> {
    const callbackUrl = options.state.manualCallback.trim();
    if (!callbackUrl) return;
    stopBrowserOauthPolling();
    captureOauthAccountBaseline();
    try {
      const response = await oauthManualCallbackRequest(callbackUrl);
      if (response.status === 'success') {
        await finalizeOauthAccountSuccess(
          callbackUrl,
          `oauth-${Date.now().toString(36)}`,
          'oauth callback accepted and account imported',
        );
        return;
      }
      const message = response.errorMessage ?? 'OAuth callback failed';
      setAddAccountErrorState(message);
    } catch (error) {
      const message = error instanceof Error ? error.message : 'OAuth callback failed';
      setAddAccountErrorState(message);
    }
  }

  function setManualCallback(value: string): void {
    setRuntimeState((previous) => applyManualCallbackState(previous, value));
  }

  function flashCopyLabel(target: 'copyAuthLabel' | 'copyDeviceLabel'): void {
    setRuntimeState((previous) => ({ ...previous, [target]: 'Copied' }));
    if (copyTimerRef.current) clearTimeout(copyTimerRef.current);
    copyTimerRef.current = setTimeout(() => {
      setRuntimeState((previous) => ({ ...previous, [target]: 'Copy' }));
    }, 1200);
  }

  async function copyBrowserAuthUrl(): Promise<void> {
    const value = options.state.browserAuthUrl.trim();
    if (!isValidOAuthAuthorizationUrl(value)) {
      options.pushRuntimeEvent(i18next.t('status.no_browser_auth_url'), 'warn');
      return;
    }
    try {
      await writeClipboardText(value);
      flashCopyLabel('copyAuthLabel');
    } catch (error) {
      const message = error instanceof Error ? error.message : i18next.t('status.failed_copy_browser_url');
      options.pushRuntimeEvent(message, 'warn');
    }
  }

  async function copyDeviceVerificationUrl(): Promise<void> {
    const value = options.state.deviceVerifyUrl.trim();
    if (!value) {
      options.pushRuntimeEvent(i18next.t('status.no_device_verification_url'), 'warn');
      return;
    }
    try {
      await writeClipboardText(value);
      flashCopyLabel('copyDeviceLabel');
    } catch (error) {
      const message = error instanceof Error ? error.message : i18next.t('status.failed_copy_device_url');
      options.pushRuntimeEvent(message, 'warn');
    }
  }

  const { startDeviceFlow, cancelDeviceFlow } = createOAuthFlowDeviceActions({
    state: options.state,
    setState: setRuntimeState,
    oauthStartRequest,
    oauthStatusRequest,
    oauthCompleteRequest,
    clearDeviceFlowTimers,
    captureOauthAccountBaseline,
    setFlowPhase,
    setFlowError,
    setAddAccountErrorState,
    finalizeOauthAccountSuccess,
    deviceTimerRef,
    oauthPollRef,
  });

  function showAddAccountError(message: string): void {
    setAddAccountErrorState(message, { reportRuntimeEvent: false });
  }

  useEffect(
    () => () => {
      stopBrowserOauthPolling();
      clearDeviceFlowTimers();
      if (copyTimerRef.current) {
        clearTimeout(copyTimerRef.current);
        copyTimerRef.current = null;
      }
    },
    [],
  );

  deepLinkHandlerRef.current = (event: OauthDeepLinkEvent) => {
    options.pushRuntimeEvent(`oauth deep-link received: ${event.kind}`);
    if (event.kind === 'success') {
      void completeBrowserOauthFromDeepLink(event.url);
      return;
    }
    startBrowserOauthPolling();
  };

  useEffect(() => {
    const unsubscribe = onOauthDeepLinkRequest((event: OauthDeepLinkEvent) => {
      deepLinkHandlerRef.current(event);
    });
    if (!unsubscribe) {
      return;
    }
    return () => {
      unsubscribe();
    };
  }, [onOauthDeepLinkRequest]);

  return {
    bootstrapOauthState,
    createListenerUrl,
    toggleListener,
    restartListener,
    initAuth0,
    captureAuthResponse,
    openAddAccountDialog,
    closeAddAccountDialog,
    setAddAccountStep,
    selectImportFile,
    submitImport,
    simulateBrowserAuth,
    submitManualCallback,
    setManualCallback,
    copyBrowserAuthUrl,
    copyDeviceVerificationUrl,
    startDeviceFlow,
    cancelDeviceFlow,
    showAddAccountError,
  };
}
