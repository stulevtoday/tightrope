import type { Dispatch, MutableRefObject, SetStateAction } from 'react';
import i18next from 'i18next';
import type { TightropeService } from '../../services/tightrope';
import type { Account, AccountUsageRefreshStatus, RuntimeAccount } from '../../shared/types';
import { errorMessage, reportWarn } from '../errors';
import {
  publishStatusNotice,
  publishStatusProgressNotice,
  type StatusNoticeLevel,
} from '../statusNotices';
import { sleep } from './useAccountsStateTransforms';

const ACCOUNT_USAGE_REFRESH_RETRY_MS = 750;
const ACCOUNT_USAGE_REFRESH_MAX_ATTEMPTS = 4;

type PushRuntimeEvent = (text: string, level?: StatusNoticeLevel) => void;

type AccountMutationService = Pick<
  TightropeService,
  | 'refreshAccountUsageTelemetryRequest'
  | 'refreshAccountTokenRequest'
  | 'pinAccountRequest'
  | 'unpinAccountRequest'
  | 'pauseAccountRequest'
  | 'reactivateAccountRequest'
  | 'deleteAccountRequest'
>;

interface AccountMutationDeps {
  service: AccountMutationService;
  accountsRef: MutableRefObject<Account[]>;
  refreshAccountsFromNative: () => Promise<RuntimeAccount[]>;
  resetRefreshErrorFlags: () => void;
  pushRuntimeEvent: PushRuntimeEvent;
}

interface RefreshUsageAfterAddDeps {
  service: Pick<TightropeService, 'refreshAccountUsageTelemetryRequest'>;
  applyRuntimeAccountPatch: (record: RuntimeAccount) => void;
  pushRuntimeEvent: PushRuntimeEvent;
}

interface RefreshTelemetryDeps extends AccountMutationDeps {
  refreshingAccountTelemetryId: string | null;
  setRefreshingAccountTelemetryId: Dispatch<SetStateAction<string | null>>;
  setTokenRefreshRequired: (accountId: string, required: boolean) => void;
  setUsageRefreshStatus: (accountId: string, status: AccountUsageRefreshStatus, message?: string | null) => void;
  isMounted?: () => boolean;
}

interface RefreshAllTelemetryDeps extends AccountMutationDeps {
  isRefreshingAllAccountTelemetry: boolean;
  setRefreshingAllAccountTelemetry: Dispatch<SetStateAction<boolean>>;
  setTokenRefreshRequired: (accountId: string, required: boolean) => void;
  setUsageRefreshStatus: (accountId: string, status: AccountUsageRefreshStatus, message?: string | null) => void;
  isMounted?: () => boolean;
}

interface RefreshTokenDeps extends AccountMutationDeps {
  refreshingAccountTokenId: string | null;
  setRefreshingAccountTokenId: Dispatch<SetStateAction<string | null>>;
  setTokenRefreshRequired: (accountId: string, required: boolean) => void;
  setUsageRefreshStatus: (accountId: string, status: AccountUsageRefreshStatus, message?: string | null) => void;
  isMounted?: () => boolean;
}

function accountFromRef(accountsRef: MutableRefObject<Account[]>, accountId: string): Account | undefined {
  return accountsRef.current.find((candidate) => candidate.id === accountId);
}

function detailIndicatesAuthRequired(detail: string): boolean {
  const normalized = detail.toLowerCase();
  return normalized.includes('token_expired') ||
    normalized.includes('authentication token is expired') ||
    normalized.includes('token expired') ||
    normalized.includes('invalid_grant') ||
    normalized.includes('not make it to this service') ||
    normalized.includes('unauthorized') ||
    normalized.includes('invalid token') ||
    normalized.includes('login required') ||
    normalized.includes('reauth');
}

async function refreshAccountsAfterMutation(deps: AccountMutationDeps): Promise<void> {
  await deps.refreshAccountsFromNative();
  deps.resetRefreshErrorFlags();
}

export async function refreshUsageTelemetryAfterAccountAdd(
  deps: RefreshUsageAfterAddDeps,
  accountId: string,
  accountName: string,
): Promise<void> {
  let lastMessage = 'Failed to refresh usage telemetry';
  for (let attempt = 0; attempt < ACCOUNT_USAGE_REFRESH_MAX_ATTEMPTS; attempt += 1) {
    try {
      const refreshed = await deps.service.refreshAccountUsageTelemetryRequest(accountId);
      deps.applyRuntimeAccountPatch(refreshed);
      deps.pushRuntimeEvent(i18next.t('status.telemetry_refreshed', { name: accountName }), 'success');
      return;
    } catch (error) {
      lastMessage = errorMessage(error, 'Failed to refresh usage telemetry');
    }
    if (attempt + 1 < ACCOUNT_USAGE_REFRESH_MAX_ATTEMPTS) {
      await sleep(ACCOUNT_USAGE_REFRESH_RETRY_MS);
    }
  }
  deps.pushRuntimeEvent(i18next.t('status.telemetry_unavailable', { message: lastMessage }), 'warn');
}

export async function toggleAccountPinAction(
  deps: AccountMutationDeps,
  accountId: string,
  nextPinned: boolean,
): Promise<void> {
  const account = accountFromRef(deps.accountsRef, accountId);
  if (!account) {
    return;
  }

  try {
    if (nextPinned) {
      await deps.service.pinAccountRequest(accountId);
    } else {
      await deps.service.unpinAccountRequest(accountId);
    }
    await refreshAccountsAfterMutation(deps);
    deps.pushRuntimeEvent(i18next.t(nextPinned ? 'status.account_pinned' : 'status.account_unpinned', { name: account.name }), 'success');
  } catch (error) {
    reportWarn(deps.pushRuntimeEvent, error, 'Failed to update account pin state');
  }
}

export async function pauseAccountAction(deps: AccountMutationDeps, accountId: string): Promise<void> {
  const account = accountFromRef(deps.accountsRef, accountId);
  if (!account) {
    return;
  }

  try {
    await deps.service.pauseAccountRequest(accountId);
    await refreshAccountsAfterMutation(deps);
    deps.pushRuntimeEvent(i18next.t('status.account_paused', { name: account.name }), 'warn');
  } catch (error) {
    reportWarn(deps.pushRuntimeEvent, error, 'Failed to pause account');
  }
}

export async function reactivateAccountAction(deps: AccountMutationDeps, accountId: string): Promise<void> {
  const account = accountFromRef(deps.accountsRef, accountId);
  if (!account) {
    return;
  }

  try {
    await deps.service.reactivateAccountRequest(accountId);
    await refreshAccountsAfterMutation(deps);
    deps.pushRuntimeEvent(i18next.t('status.account_resumed', { name: account.name }), 'success');
  } catch (error) {
    reportWarn(deps.pushRuntimeEvent, error, 'Failed to resume account');
  }
}

export async function deleteAccountAction(deps: AccountMutationDeps, accountId: string): Promise<boolean> {
  const account = accountFromRef(deps.accountsRef, accountId);
  if (!account) {
    return false;
  }

  try {
    await deps.service.deleteAccountRequest(accountId);
    await refreshAccountsAfterMutation(deps);
    deps.pushRuntimeEvent(i18next.t('status.account_deleted', { name: account.name }), 'warn');
    return true;
  } catch (error) {
    reportWarn(deps.pushRuntimeEvent, error, 'Failed to delete account');
    return false;
  }
}

export async function refreshAccountTelemetryAction(deps: RefreshTelemetryDeps, accountId: string): Promise<void> {
  if (deps.isMounted && !deps.isMounted()) {
    return;
  }
  const account = accountFromRef(deps.accountsRef, accountId);
  if (!account) {
    return;
  }
  if (deps.refreshingAccountTelemetryId === accountId) {
    return;
  }

  if (!deps.isMounted || deps.isMounted()) {
    deps.setRefreshingAccountTelemetryId(accountId);
  }
  try {
    await deps.service.refreshAccountUsageTelemetryRequest(accountId);
    deps.setTokenRefreshRequired(accountId, false);
    deps.setUsageRefreshStatus(accountId, 'success', null);
    await refreshAccountsAfterMutation(deps);
    deps.pushRuntimeEvent(i18next.t('status.telemetry_refreshed', { name: account.name }), 'success');
  } catch (error) {
    const detail = errorMessage(error, 'Failed to refresh usage telemetry');
    const authRequired = detailIndicatesAuthRequired(detail);
    deps.setTokenRefreshRequired(accountId, authRequired);
    deps.setUsageRefreshStatus(accountId, authRequired ? 'auth_required' : 'failed', detail);
    deps.pushRuntimeEvent(
      `Failed to refresh usage telemetry: ${account.name} (${account.id}) — ${detail}`,
      'warn',
    );
  } finally {
    if (!deps.isMounted || deps.isMounted()) {
      deps.setRefreshingAccountTelemetryId((current) => (current === accountId ? null : current));
    }
  }
}

export async function refreshAllAccountsTelemetryAction(deps: RefreshAllTelemetryDeps): Promise<void> {
  if (deps.isMounted && !deps.isMounted()) {
    return;
  }
  if (deps.isRefreshingAllAccountTelemetry) {
    return;
  }

  if (!deps.isMounted || deps.isMounted()) {
    deps.setRefreshingAllAccountTelemetry(true);
  }

  let refreshedCount = 0;
  let prunedCount = 0;
  let failedCount = 0;
  const deferredRuntimeEvents: Array<{ text: string; level: StatusNoticeLevel }> = [];
  const statusByAccountId = new Map<string, {
    status: AccountUsageRefreshStatus;
    message: string | null;
    tokenRefreshRequired: boolean;
  }>();

  const recordStatus = (
    accountId: string,
    status: AccountUsageRefreshStatus,
    message: string | null,
    tokenRefreshRequired: boolean,
  ): void => {
    deps.setTokenRefreshRequired(accountId, tokenRefreshRequired);
    deps.setUsageRefreshStatus(accountId, status, message);
    statusByAccountId.set(accountId, { status, message, tokenRefreshRequired });
  };

  try {
    const queued = [...deps.accountsRef.current];
    if (queued.length === 0) {
      deps.pushRuntimeEvent(i18next.t('status.telemetry_queue_no_accounts'), 'warn');
      return;
    }
    const total = queued.length;
    let processedCount = 0;
    const publishProgress = (): void => {
      publishStatusProgressNotice({
        label: i18next.t('status.telemetry_queue_refreshing'),
        current: processedCount,
        total,
        level: failedCount > 0 ? 'warn' : 'info',
      });
    };
    publishProgress();

    for (const queuedAccount of queued) {
      if (deps.isMounted && !deps.isMounted()) {
        for (const event of deferredRuntimeEvents) {
          deps.pushRuntimeEvent(event.text, event.level);
        }
        publishStatusNotice({ message: i18next.t('status.telemetry_queue_cancelled'), level: 'warn' });
        return;
      }
      const current = accountFromRef(deps.accountsRef, queuedAccount.id);
      if (!current) {
        processedCount += 1;
        publishProgress();
        continue;
      }

      if (current.state === 'deactivated') {
        try {
          await deps.service.deleteAccountRequest(current.id);
          prunedCount += 1;
          processedCount += 1;
          publishProgress();
        } catch (error) {
          failedCount += 1;
          processedCount += 1;
          publishProgress();
          const detail = errorMessage(error, `Failed to prune deactivated account: ${current.name}`);
          recordStatus(current.id, 'auth_required', detail, true);
          deferredRuntimeEvents.push({
            text: `Failed to prune deactivated account: ${current.name} (${current.id}) — ${detail}`,
            level: 'warn',
          });
        }
        continue;
      }

      try {
        const refreshed = await deps.service.refreshAccountUsageTelemetryRequest(current.id);
        recordStatus(current.id, 'success', null, false);
        if (refreshed.status === 'deactivated') {
          try {
            await deps.service.deleteAccountRequest(current.id);
            prunedCount += 1;
            processedCount += 1;
            publishProgress();
          } catch (deleteError) {
            failedCount += 1;
            processedCount += 1;
            publishProgress();
            const detail = errorMessage(deleteError, `Failed to prune deactivated account: ${current.name}`);
            recordStatus(current.id, 'auth_required', detail, true);
            deferredRuntimeEvents.push({
              text: `Failed to prune deactivated account: ${current.name} (${current.id}) — ${detail}`,
              level: 'warn',
            });
          }
          continue;
        }
        refreshedCount += 1;
        processedCount += 1;
        publishProgress();
      } catch (error) {
        failedCount += 1;
        processedCount += 1;
        publishProgress();
        const detail = errorMessage(error, `Failed to refresh usage telemetry: ${current.name}`);
        const authRequired = detailIndicatesAuthRequired(detail);
        recordStatus(current.id, authRequired ? 'auth_required' : 'failed', detail, authRequired);
        deferredRuntimeEvents.push({
          text: `Failed to refresh usage telemetry: ${current.name} (${current.id}) — ${detail}`,
          level: 'warn',
        });
      }
    }

    await refreshAccountsAfterMutation(deps);
    for (const [accountId, update] of statusByAccountId) {
      deps.setTokenRefreshRequired(accountId, update.tokenRefreshRequired);
      deps.setUsageRefreshStatus(accountId, update.status, update.message);
    }
    for (const event of deferredRuntimeEvents) {
      deps.pushRuntimeEvent(event.text, event.level);
    }

    const summary = `telemetry refresh queue complete: ${refreshedCount} refreshed, ${prunedCount} pruned, ${failedCount} failed`;
    deps.pushRuntimeEvent(summary, failedCount > 0 ? 'warn' : 'success');
  } finally {
    if (!deps.isMounted || deps.isMounted()) {
      deps.setRefreshingAllAccountTelemetry(false);
    }
  }
}

export async function refreshAccountTokenAction(deps: RefreshTokenDeps, accountId: string): Promise<void> {
  if (deps.isMounted && !deps.isMounted()) {
    return;
  }
  const account = accountFromRef(deps.accountsRef, accountId);
  if (!account) {
    return;
  }
  if (deps.refreshingAccountTokenId === accountId) {
    return;
  }

  if (!deps.isMounted || deps.isMounted()) {
    deps.setRefreshingAccountTokenId(accountId);
  }
  try {
    await deps.service.refreshAccountTokenRequest(accountId);
    deps.setTokenRefreshRequired(accountId, false);
    deps.setUsageRefreshStatus(accountId, 'success', null);
    await refreshAccountsAfterMutation(deps);
    deps.pushRuntimeEvent(i18next.t('status.token_refreshed', { name: account.name }), 'success');
  } catch (error) {
    const detail = errorMessage(error, 'Failed to refresh account token');
    const authRequired = detailIndicatesAuthRequired(detail);
    deps.setTokenRefreshRequired(accountId, authRequired);
    deps.setUsageRefreshStatus(accountId, authRequired ? 'auth_required' : 'failed', detail);
    deps.pushRuntimeEvent(i18next.t('status.token_refresh_failed', { name: account.name, id: account.id, detail }), 'warn');
  } finally {
    if (!deps.isMounted || deps.isMounted()) {
      deps.setRefreshingAccountTokenId((current) => (current === accountId ? null : current));
    }
  }
}
