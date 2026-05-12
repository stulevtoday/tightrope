import type {
  AppMetaResponse,
  ClusterStatus,
  DashboardSettings,
  DashboardSettingsUpdate,
  FirewallListResponse,
  ManualCallbackResponse,
  OauthDeepLinkEvent,
  OauthCompleteResponse,
  OauthStartResponse,
  OauthStatusResponse,
  RuntimeAccount,
  RuntimeAccountTraffic,
  RuntimeAccountTrafficResponse,
  RuntimeRequestLog,
  RuntimeRequestLogsResponse,
  RuntimeStickySessionsPurgeResponse,
  RuntimeStickySessionsResponse,
  SqlImportApplyRequestPayload,
  SqlImportApplyResponse,
  SqlImportPreviewRequestPayload,
  SqlImportPreviewResponse,
  SyncEvent,
} from '../shared/types';

const RUNTIME_HTTP_HOST = '127.0.0.1:2455';

export interface RuntimeBackendStateResponse {
  enabled: boolean;
}

export interface TightropeService {
  getAppMetaRequest: () => Promise<AppMetaResponse | null>;
  oauthStartRequest: (forceMethod: 'browser' | 'device') => Promise<OauthStartResponse>;
  oauthStatusRequest: () => Promise<OauthStatusResponse>;
  oauthStopRequest: () => Promise<OauthStatusResponse>;
  oauthRestartRequest: () => Promise<OauthStartResponse>;
  oauthCompleteRequest: (payload: { deviceAuthId?: string; userCode?: string }) => Promise<OauthCompleteResponse>;
  oauthManualCallbackRequest: (callbackUrl: string) => Promise<ManualCallbackResponse>;
  listAccountsRequest: () => Promise<RuntimeAccount[]>;
  listStickySessionsRequest: (limit: number, offset: number) => Promise<RuntimeStickySessionsResponse>;
  purgeStaleSessionsRequest: () => Promise<RuntimeStickySessionsPurgeResponse>;
  listRequestLogsRequest: (limit: number, offset: number) => Promise<RuntimeRequestLog[]>;
  listAccountTrafficRequest: () => Promise<RuntimeAccountTraffic[]>;
  backendStatusRequest: () => Promise<RuntimeBackendStateResponse>;
  backendStartRequest: () => Promise<RuntimeBackendStateResponse>;
  backendStopRequest: () => Promise<RuntimeBackendStateResponse>;
  importAccountRequest: (email: string, provider: string, accessToken?: string, refreshToken?: string) => Promise<RuntimeAccount>;
  previewSqlImportRequest: (payload: SqlImportPreviewRequestPayload) => Promise<SqlImportPreviewResponse>;
  pickSqlImportSourcePathRequest: () => Promise<string | null>;
  applySqlImportRequest: (payload: SqlImportApplyRequestPayload) => Promise<SqlImportApplyResponse>;
  pinAccountRequest: (accountId: string) => Promise<void>;
  unpinAccountRequest: (accountId: string) => Promise<void>;
  pauseAccountRequest: (accountId: string) => Promise<void>;
  reactivateAccountRequest: (accountId: string) => Promise<void>;
  deleteAccountRequest: (accountId: string) => Promise<void>;
  refreshAccountUsageTelemetryRequest: (accountId: string) => Promise<RuntimeAccount>;
  refreshAccountTokenRequest: (accountId: string) => Promise<RuntimeAccount>;
  getSettingsRequest: () => Promise<DashboardSettings | null>;
  updateSettingsRequest: (update: DashboardSettingsUpdate) => Promise<DashboardSettings | null>;
  changeDatabasePassphraseRequest: (currentPassphrase: string, nextPassphrase: string) => Promise<void>;
  exportDatabaseRequest: () => Promise<{ success: boolean; error?: string }>;
  listFirewallIpsRequest: () => Promise<FirewallListResponse | null>;
  addFirewallIpRequest: (ipAddress: string) => Promise<boolean>;
  removeFirewallIpRequest: (ipAddress: string) => Promise<boolean>;
  getClusterStatusRequest: () => Promise<ClusterStatus | null>;
  clusterEnableRequest: (config: unknown) => Promise<boolean>;
  clusterDisableRequest: () => Promise<boolean>;
  addPeerRequest: (address: string) => Promise<boolean>;
  removePeerRequest: (siteId: string) => Promise<boolean>;
  triggerSyncRequest: () => Promise<boolean>;
  onOauthDeepLinkRequest: (listener: (event: OauthDeepLinkEvent) => void) => (() => void) | null;
  onAboutOpenRequest: (listener: () => void) => (() => void) | null;
  onSyncEventRequest: (listener: (event: SyncEvent) => void) => (() => void) | null;
  platformRequest: () => NodeJS.Platform | null;
  windowCloseRequest: () => Promise<boolean>;
  windowMinimizeRequest: () => Promise<boolean>;
  windowToggleMaximizeRequest: () => Promise<boolean>;
  windowIsMaximizedRequest: () => Promise<boolean | null>;
}

async function parseJsonResponse<T>(response: Response): Promise<T> {
  const json = (await response.json().catch(() => null)) as T | null;
  if (response.ok && json !== null) {
    return json;
  }
  const message = (json as { error?: { message?: string } } | null)?.error?.message ?? `HTTP ${response.status}`;
  throw new Error(message);
}

async function postJson<T>(path: string, body: unknown): Promise<T> {
  const response = await fetch(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  return parseJsonResponse<T>(response);
}

async function getJson<T>(path: string): Promise<T> {
  const response = await fetch(path, { method: 'GET' });
  return parseJsonResponse<T>(response);
}

function runtimeHttpUrl(path: string): string {
  const normalized = path.startsWith('/') ? path : `/${path}`;
  return `http://${RUNTIME_HTTP_HOST}${normalized}`;
}

function isObjectRecord(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === 'object';
}

function isRuntimeErrorEnvelope(value: unknown): value is { error: { code?: string; message: string } } {
  if (!isObjectRecord(value)) {
    return false;
  }
  const candidate = value.error;
  if (!isObjectRecord(candidate) || typeof candidate.message !== 'string') {
    return false;
  }
  return !('code' in candidate) || typeof candidate.code === 'string' || typeof candidate.code === 'undefined';
}

function stringOrNull(value: unknown): string | null {
  return typeof value === 'string' ? value : null;
}

function numberOrNull(value: unknown): number | null {
  return typeof value === 'number' && Number.isFinite(value) ? value : null;
}

function coerceAppMetaResponse(value: unknown): AppMetaResponse | null {
  if (!isObjectRecord(value)) {
    return null;
  }
  const version = stringOrNull(value.version);
  const buildChannel = stringOrNull(value.buildChannel);
  const packaged = typeof value.packaged === 'boolean' ? value.packaged : null;
  if (!version || !buildChannel || packaged === null) {
    return null;
  }
  return { version, buildChannel, packaged };
}

function coerceOauthStartResponse(value: unknown, methodFallback: 'browser' | 'device'): OauthStartResponse {
  if (!isObjectRecord(value)) {
    return {
      method: methodFallback,
      authorizationUrl: null,
      callbackUrl: null,
      verificationUrl: null,
      userCode: null,
      deviceAuthId: null,
      intervalSeconds: null,
      expiresInSeconds: null,
    };
  }
  return {
    method: stringOrNull(value.method) ?? methodFallback,
    authorizationUrl: stringOrNull(value.authorizationUrl),
    callbackUrl: stringOrNull(value.callbackUrl),
    verificationUrl: stringOrNull(value.verificationUrl),
    userCode: stringOrNull(value.userCode),
    deviceAuthId: stringOrNull(value.deviceAuthId),
    intervalSeconds: numberOrNull(value.intervalSeconds),
    expiresInSeconds: numberOrNull(value.expiresInSeconds),
  };
}

function coerceOauthStatusResponse(value: unknown): OauthStatusResponse {
  if (!isObjectRecord(value)) {
    return {
      status: 'idle',
      errorMessage: 'Malformed OAuth status payload',
      callbackUrl: null,
      authorizationUrl: null,
    };
  }
  const status = stringOrNull(value.status);
  const errorMessage = stringOrNull(value.errorMessage);
  return {
    status: status ?? 'idle',
    errorMessage: errorMessage ?? (status ? null : 'Malformed OAuth status payload'),
    listenerRunning: typeof value.listenerRunning === 'boolean' ? value.listenerRunning : undefined,
    callbackUrl: stringOrNull(value.callbackUrl),
    authorizationUrl: stringOrNull(value.authorizationUrl),
  };
}

function coerceOauthCompleteResponse(value: unknown): OauthCompleteResponse {
  if (!isObjectRecord(value)) {
    return { status: 'error' };
  }
  return { status: stringOrNull(value.status) ?? 'error' };
}

function coerceManualCallbackResponse(value: unknown): ManualCallbackResponse {
  if (!isObjectRecord(value)) {
    return { status: 'error', errorMessage: 'Malformed manual callback payload' };
  }
  return {
    status: stringOrNull(value.status) ?? 'error',
    errorMessage: stringOrNull(value.errorMessage),
  };
}

function coerceRuntimeAccount(value: unknown): RuntimeAccount | null {
  if (!isObjectRecord(value)) {
    return null;
  }
  const accountId = stringOrNull(value.accountId);
  const email = stringOrNull(value.email);
  const provider = stringOrNull(value.provider);
  const status = stringOrNull(value.status);
  if (!accountId || !email || !provider || !status) {
    return null;
  }
  return value as unknown as RuntimeAccount;
}

function coerceRuntimeAccounts(value: unknown): RuntimeAccount[] {
  if (!Array.isArray(value)) {
    return [];
  }
  return value.map(coerceRuntimeAccount).filter((account): account is RuntimeAccount => account !== null);
}

function coerceRuntimeStickySessionsResponse(value: unknown): RuntimeStickySessionsResponse {
  if (!isObjectRecord(value)) {
    return { generatedAtMs: Date.now(), sessions: [] };
  }
  const generatedAtMs = numberOrNull(value.generatedAtMs) ?? Date.now();
  const sessions = Array.isArray(value.sessions) ? value.sessions : [];
  return {
    generatedAtMs,
    sessions: sessions as RuntimeStickySessionsResponse['sessions'],
  };
}

function coerceRuntimeStickySessionsPurgeResponse(value: unknown): RuntimeStickySessionsPurgeResponse {
  if (!isObjectRecord(value)) {
    return { generatedAtMs: Date.now(), purged: 0 };
  }
  return {
    generatedAtMs: numberOrNull(value.generatedAtMs) ?? Date.now(),
    purged: Math.max(0, Math.trunc(numberOrNull(value.purged) ?? 0)),
  };
}

function coerceRuntimeRequestLogs(value: unknown): RuntimeRequestLog[] {
  if (!Array.isArray(value)) {
    return [];
  }
  return value.filter((entry): entry is RuntimeRequestLog => {
    if (!isObjectRecord(entry)) {
      return false;
    }
    return (
      typeof entry.id === 'number' &&
      typeof entry.path === 'string' &&
      typeof entry.method === 'string' &&
      typeof entry.statusCode === 'number' &&
      typeof entry.requestedAt === 'string' &&
      typeof entry.totalCost === 'number'
    );
  });
}

function coerceRuntimeAccountTraffic(value: unknown): RuntimeAccountTraffic[] {
  if (!Array.isArray(value)) {
    return [];
  }
  return value.filter((entry): entry is RuntimeAccountTraffic => {
    if (!isObjectRecord(entry)) {
      return false;
    }
    return (
      typeof entry.accountId === 'string' &&
      typeof entry.upBytes === 'number' &&
      typeof entry.downBytes === 'number' &&
      typeof entry.lastUpAtMs === 'number' &&
      typeof entry.lastDownAtMs === 'number'
    );
  });
}

function coerceRuntimeBackendStateResponse(value: unknown): RuntimeBackendStateResponse {
  if (!isObjectRecord(value) || typeof value.enabled !== 'boolean') {
    return { enabled: false };
  }
  return { enabled: value.enabled };
}

function coerceSqlImportAction(value: unknown): SqlImportPreviewResponse['rows'][number]['action'] {
  if (value === 'new' || value === 'update' || value === 'skip' || value === 'invalid') {
    return value;
  }
  return 'skip';
}

function coerceSqlImportPreviewResponse(value: unknown): SqlImportPreviewResponse {
  const empty: SqlImportPreviewResponse = {
    source: {
      path: '',
      fileName: '',
      sizeBytes: 0,
      modifiedAtMs: 0,
      schemaFingerprint: '',
      truncated: false,
    },
    totals: {
      scanned: 0,
      newCount: 0,
      updateCount: 0,
      skipCount: 0,
      invalidCount: 0,
    },
    requiresSourceEncryptionKey: false,
    requiresSourceDatabasePassphrase: false,
    rows: [],
    warnings: [],
  };
  if (!isObjectRecord(value)) {
    return empty;
  }

  const source = isObjectRecord(value.source) ? value.source : {};
  const totals = isObjectRecord(value.totals) ? value.totals : {};
  const warnings = Array.isArray(value.warnings) ? value.warnings.filter((entry): entry is string => typeof entry === 'string') : [];
  const rows = Array.isArray(value.rows)
    ? value.rows
        .filter((entry): entry is Record<string, unknown> => isObjectRecord(entry))
        .map((entry) => ({
          sourceRowId: stringOrNull(entry.sourceRowId) ?? '',
          dedupeKey: stringOrNull(entry.dedupeKey) ?? '',
          email: stringOrNull(entry.email),
          provider: stringOrNull(entry.provider),
          planType: stringOrNull(entry.planType),
          hasAccessToken: entry.hasAccessToken === true,
          hasRefreshToken: entry.hasRefreshToken === true,
          hasIdToken: entry.hasIdToken === true,
          action: coerceSqlImportAction(entry.action),
          reason: stringOrNull(entry.reason) ?? '',
        }))
        .filter((entry) => entry.sourceRowId.length > 0)
    : [];

  return {
    source: {
      path: stringOrNull(source.path) ?? '',
      fileName: stringOrNull(source.fileName) ?? '',
      sizeBytes: numberOrNull(source.sizeBytes) ?? 0,
      modifiedAtMs: numberOrNull(source.modifiedAtMs) ?? 0,
      schemaFingerprint: stringOrNull(source.schemaFingerprint) ?? '',
      truncated: source.truncated === true,
    },
    totals: {
      scanned: numberOrNull(totals.scanned) ?? 0,
      newCount: numberOrNull(totals.newCount) ?? 0,
      updateCount: numberOrNull(totals.updateCount) ?? 0,
      skipCount: numberOrNull(totals.skipCount) ?? 0,
      invalidCount: numberOrNull(totals.invalidCount) ?? 0,
    },
    requiresSourceEncryptionKey: value.requiresSourceEncryptionKey === true,
    requiresSourceDatabasePassphrase: value.requiresSourceDatabasePassphrase === true,
    rows,
    warnings,
  };
}

function coerceSqlImportApplyResponse(value: unknown): SqlImportApplyResponse {
  const empty: SqlImportApplyResponse = {
    totals: {
      scanned: 0,
      inserted: 0,
      updated: 0,
      skipped: 0,
      invalid: 0,
      failed: 0,
    },
    warnings: [],
  };
  if (!isObjectRecord(value)) {
    return empty;
  }
  const totals = isObjectRecord(value.totals) ? value.totals : {};
  const warnings = Array.isArray(value.warnings) ? value.warnings.filter((entry): entry is string => typeof entry === 'string') : [];
  return {
    totals: {
      scanned: numberOrNull(totals.scanned) ?? 0,
      inserted: numberOrNull(totals.inserted) ?? 0,
      updated: numberOrNull(totals.updated) ?? 0,
      skipped: numberOrNull(totals.skipped) ?? 0,
      invalid: numberOrNull(totals.invalid) ?? 0,
      failed: numberOrNull(totals.failed) ?? 0,
    },
    warnings,
  };
}

export async function oauthStartRequest(forceMethod: 'browser' | 'device'): Promise<OauthStartResponse> {
  const api = window.tightrope;
  if (api?.oauthStart) {
    const response = await api.oauthStart({ forceMethod });
    return coerceOauthStartResponse(response, forceMethod);
  }
  const response = await postJson<OauthStartResponse>('/api/oauth/start', { forceMethod });
  return coerceOauthStartResponse(response, forceMethod);
}

export async function getAppMetaRequest(): Promise<AppMetaResponse | null> {
  const api = window.tightrope;
  if (!api?.getAppMeta) {
    return null;
  }
  const response = await api.getAppMeta();
  return coerceAppMetaResponse(response);
}

export async function oauthStatusRequest(): Promise<OauthStatusResponse> {
  const api = window.tightrope;
  if (api?.oauthStatus) {
    const response = await api.oauthStatus();
    return coerceOauthStatusResponse(response);
  }
  const response = await getJson<OauthStatusResponse>('/api/oauth/status');
  return coerceOauthStatusResponse(response);
}

export async function oauthStopRequest(): Promise<OauthStatusResponse> {
  const api = window.tightrope;
  if (api?.oauthStop) {
    const response = await api.oauthStop();
    return coerceOauthStatusResponse(response);
  }
  const response = await postJson<OauthStatusResponse>('/api/oauth/stop', {});
  return coerceOauthStatusResponse(response);
}

export async function oauthRestartRequest(): Promise<OauthStartResponse> {
  const api = window.tightrope;
  if (api?.oauthRestart) {
    const response = await api.oauthRestart();
    return coerceOauthStartResponse(response, 'browser');
  }
  const response = await postJson<OauthStartResponse>('/api/oauth/restart', {});
  return coerceOauthStartResponse(response, 'browser');
}

export async function oauthCompleteRequest(payload: {
  deviceAuthId?: string;
  userCode?: string;
}): Promise<OauthCompleteResponse> {
  const api = window.tightrope;
  if (api?.oauthComplete) {
    const response = await api.oauthComplete(payload);
    return coerceOauthCompleteResponse(response);
  }
  const response = await postJson<OauthCompleteResponse>('/api/oauth/complete', payload);
  return coerceOauthCompleteResponse(response);
}

export async function oauthManualCallbackRequest(callbackUrl: string): Promise<ManualCallbackResponse> {
  const api = window.tightrope;
  if (api?.oauthManualCallback) {
    const response = await api.oauthManualCallback(callbackUrl);
    return coerceManualCallbackResponse(response);
  }
  const response = await postJson<ManualCallbackResponse>('/api/oauth/manual-callback', { callbackUrl });
  return coerceManualCallbackResponse(response);
}

export async function listAccountsRequest(): Promise<RuntimeAccount[]> {
  const api = window.tightrope;
  if (api?.listAccounts) {
    const response = await api.listAccounts();
    if (isRuntimeErrorEnvelope(response)) {
      const error = new Error(response.error.message) as Error & { code?: string };
      error.code = response.error.code;
      throw error;
    }
    return coerceRuntimeAccounts(response.accounts);
  }
  const response = await getJson<{ accounts?: RuntimeAccount[] }>('/api/accounts');
  return coerceRuntimeAccounts(response.accounts);
}

export async function listStickySessionsRequest(limit: number, offset: number): Promise<RuntimeStickySessionsResponse> {
  const api = window.tightrope;
  if (api?.listStickySessions) {
    const response = await api.listStickySessions({ limit, offset });
    if (isRuntimeErrorEnvelope(response)) {
      const error = new Error(response.error.message) as Error & { code?: string };
      error.code = response.error.code;
      throw error;
    }
    return coerceRuntimeStickySessionsResponse(response);
  }
  const response = await getJson<RuntimeStickySessionsResponse>(runtimeHttpUrl(`/api/sessions?limit=${limit}&offset=${offset}`));
  return coerceRuntimeStickySessionsResponse(response);
}

export async function purgeStaleSessionsRequest(): Promise<RuntimeStickySessionsPurgeResponse> {
  const api = window.tightrope;
  if (api?.purgeStaleSessions) {
    const response = await api.purgeStaleSessions();
    if (isRuntimeErrorEnvelope(response)) {
      const error = new Error(response.error.message) as Error & { code?: string };
      error.code = response.error.code;
      throw error;
    }
    return coerceRuntimeStickySessionsPurgeResponse(response);
  }
  const response = await postJson<RuntimeStickySessionsPurgeResponse>(runtimeHttpUrl('/api/sessions/purge-stale'), {});
  return coerceRuntimeStickySessionsPurgeResponse(response);
}

export async function listRequestLogsRequest(limit: number, offset: number): Promise<RuntimeRequestLog[]> {
  const api = window.tightrope;
  if (api?.listRequestLogs) {
    const response = await api.listRequestLogs({ limit, offset });
    return coerceRuntimeRequestLogs(response.logs);
  }
  const response = await getJson<RuntimeRequestLogsResponse>(runtimeHttpUrl(`/api/logs?limit=${limit}&offset=${offset}`));
  return coerceRuntimeRequestLogs(response.logs);
}

export async function listAccountTrafficRequest(): Promise<RuntimeAccountTraffic[]> {
  const api = window.tightrope;
  if (api?.listAccountTraffic) {
    const response = await api.listAccountTraffic();
    return coerceRuntimeAccountTraffic(response.accounts);
  }
  const response = await getJson<RuntimeAccountTrafficResponse>(runtimeHttpUrl('/api/accounts/traffic'));
  return coerceRuntimeAccountTraffic(response.accounts);
}

export async function backendStatusRequest(): Promise<RuntimeBackendStateResponse> {
  const api = window.tightrope;
  if (api?.getBackendStatus) {
    const response = await api.getBackendStatus();
    return coerceRuntimeBackendStateResponse(response);
  }
  const response = await getJson<RuntimeBackendStateResponse>(runtimeHttpUrl('/api/runtime/proxy'));
  return coerceRuntimeBackendStateResponse(response);
}

export async function backendStartRequest(): Promise<RuntimeBackendStateResponse> {
  const api = window.tightrope;
  if (api?.startBackend) {
    const response = await api.startBackend();
    return coerceRuntimeBackendStateResponse(response);
  }
  const response = await postJson<RuntimeBackendStateResponse>(runtimeHttpUrl('/api/runtime/proxy/start'), {});
  return coerceRuntimeBackendStateResponse(response);
}

export async function backendStopRequest(): Promise<RuntimeBackendStateResponse> {
  const api = window.tightrope;
  if (api?.stopBackend) {
    const response = await api.stopBackend();
    return coerceRuntimeBackendStateResponse(response);
  }
  const response = await postJson<RuntimeBackendStateResponse>(runtimeHttpUrl('/api/runtime/proxy/stop'), {});
  return coerceRuntimeBackendStateResponse(response);
}

export async function importAccountRequest(email: string, provider: string, accessToken?: string, refreshToken?: string): Promise<RuntimeAccount> {
  const payload: { email: string; provider: string; access_token?: string; refresh_token?: string } = { email, provider };
  if (accessToken) payload.access_token = accessToken;
  if (refreshToken) payload.refresh_token = refreshToken;
  const api = window.tightrope;
  if (api?.importAccount) {
    const response = await api.importAccount(payload);
    const account = coerceRuntimeAccount(response);
    if (account) {
      return account;
    }
    throw new Error('Malformed account payload');
  }
  const response = await postJson<RuntimeAccount>('/api/accounts/import', payload);
  const account = coerceRuntimeAccount(response);
  if (account) {
    return account;
  }
  throw new Error('Malformed account payload');
}

export async function previewSqlImportRequest(
  payload: SqlImportPreviewRequestPayload,
): Promise<SqlImportPreviewResponse> {
  const api = window.tightrope;
  if (api?.previewSqlImport) {
    const response = await api.previewSqlImport(payload);
    return coerceSqlImportPreviewResponse(response);
  }
  const response = await postJson<SqlImportPreviewResponse>('/api/accounts/import/sqlite/preview', payload);
  return coerceSqlImportPreviewResponse(response);
}

export async function pickSqlImportSourcePathRequest(): Promise<string | null> {
  const api = window.tightrope;
  if (!api?.pickSqlImportSourcePath) {
    return null;
  }
  const response = await api.pickSqlImportSourcePath();
  if (typeof response !== 'string') {
    return null;
  }
  const trimmed = response.trim();
  return trimmed.length > 0 ? trimmed : null;
}

export async function applySqlImportRequest(payload: SqlImportApplyRequestPayload): Promise<SqlImportApplyResponse> {
  const api = window.tightrope;
  if (api?.applySqlImport) {
    const response = await api.applySqlImport(payload);
    return coerceSqlImportApplyResponse(response);
  }
  const response = await postJson<SqlImportApplyResponse>('/api/accounts/import/sqlite/apply', payload);
  return coerceSqlImportApplyResponse(response);
}

export async function pinAccountRequest(accountId: string): Promise<void> {
  const api = window.tightrope;
  if (api?.pinAccount) {
    await api.pinAccount(accountId);
    return;
  }
  await postJson<{ status: string }>(`/api/accounts/${encodeURIComponent(accountId)}/pin`, {});
}

export async function unpinAccountRequest(accountId: string): Promise<void> {
  const api = window.tightrope;
  if (api?.unpinAccount) {
    await api.unpinAccount(accountId);
    return;
  }
  await postJson<{ status: string }>(`/api/accounts/${encodeURIComponent(accountId)}/unpin`, {});
}

export async function pauseAccountRequest(accountId: string): Promise<void> {
  const api = window.tightrope;
  if (api?.pauseAccount) {
    await api.pauseAccount(accountId);
    return;
  }
  await postJson<{ status: string }>(`/api/accounts/${encodeURIComponent(accountId)}/pause`, {});
}

export async function reactivateAccountRequest(accountId: string): Promise<void> {
  const api = window.tightrope;
  if (api?.reactivateAccount) {
    await api.reactivateAccount(accountId);
    return;
  }
  await postJson<{ status: string }>(`/api/accounts/${encodeURIComponent(accountId)}/reactivate`, {});
}

export async function deleteAccountRequest(accountId: string): Promise<void> {
  const api = window.tightrope;
  if (api?.deleteAccount) {
    await api.deleteAccount(accountId);
    return;
  }
  const response = await fetch(`/api/accounts/${encodeURIComponent(accountId)}`, { method: 'DELETE' });
  await parseJsonResponse<{ status: string }>(response);
}

export async function refreshAccountUsageTelemetryRequest(accountId: string): Promise<RuntimeAccount> {
  const api = window.tightrope;
  if (api?.refreshAccountUsageTelemetry) {
    const response = await api.refreshAccountUsageTelemetry(accountId);
    if (isRuntimeErrorEnvelope(response)) {
      const error = new Error(response.error.message) as Error & { code?: string };
      error.code = response.error.code;
      throw error;
    }
    const account = coerceRuntimeAccount(response);
    if (account) {
      return account;
    }
    throw new Error('Malformed account telemetry payload');
  }
  const response = await postJson<RuntimeAccount>(`/api/accounts/${encodeURIComponent(accountId)}/refresh-usage`, {});
  const account = coerceRuntimeAccount(response);
  if (account) {
    return account;
  }
  throw new Error('Malformed account telemetry payload');
}

export async function refreshAccountTokenRequest(accountId: string): Promise<RuntimeAccount> {
  const api = window.tightrope;
  if (api?.refreshAccountToken) {
    const response = await api.refreshAccountToken(accountId);
    if (isRuntimeErrorEnvelope(response)) {
      const error = new Error(response.error.message) as Error & { code?: string };
      error.code = response.error.code;
      throw error;
    }
    const account = coerceRuntimeAccount(response);
    if (account) {
      return account;
    }
    throw new Error('Malformed account token refresh payload');
  }
  const response = await postJson<RuntimeAccount>(`/api/accounts/${encodeURIComponent(accountId)}/refresh-token`, {});
  const account = coerceRuntimeAccount(response);
  if (account) {
    return account;
  }
  throw new Error('Malformed account token refresh payload');
}

export async function getSettingsRequest(): Promise<DashboardSettings | null> {
  const api = window.tightrope;
  if (!api?.getSettings) {
    return null;
  }
  return api.getSettings();
}

export async function updateSettingsRequest(update: DashboardSettingsUpdate): Promise<DashboardSettings | null> {
  const api = window.tightrope;
  if (!api?.updateSettings) {
    return null;
  }
  return api.updateSettings(update);
}

export async function changeDatabasePassphraseRequest(
  currentPassphrase: string,
  nextPassphrase: string,
): Promise<void> {
  const api = window.tightrope;
  if (!api?.changeDatabasePassphrase) {
    throw new Error('Database passphrase change is unavailable');
  }

  if (currentPassphrase.length === 0) {
    throw new Error('Current passphrase is required');
  }
  if (nextPassphrase.length < 8) {
    throw new Error('New passphrase must be at least 8 characters');
  }

  await api.changeDatabasePassphrase({ currentPassphrase, nextPassphrase });
}

export async function exportDatabaseRequest(): Promise<{ success: boolean; error?: string }> {
  const api = window.tightrope;
  if (!api?.exportDatabase) {
    return { success: false, error: 'Database export is unavailable' };
  }
  return api.exportDatabase();
}

export async function listFirewallIpsRequest(): Promise<FirewallListResponse | null> {
  const api = window.tightrope;
  if (!api?.listFirewallIps) {
    return null;
  }
  return api.listFirewallIps();
}

export async function addFirewallIpRequest(ipAddress: string): Promise<boolean> {
  const api = window.tightrope;
  if (!api?.addFirewallIp) {
    return false;
  }
  await api.addFirewallIp(ipAddress);
  return true;
}

export async function removeFirewallIpRequest(ipAddress: string): Promise<boolean> {
  const api = window.tightrope;
  if (!api?.removeFirewallIp) {
    return false;
  }
  await api.removeFirewallIp(ipAddress);
  return true;
}

export async function getClusterStatusRequest(): Promise<ClusterStatus | null> {
  const api = window.tightrope;
  if (!api?.getClusterStatus) {
    return null;
  }
  const response = await api.getClusterStatus();
  if (response && typeof response === 'object') {
    return response;
  }
  return null;
}

export async function clusterEnableRequest(config: unknown): Promise<boolean> {
  const api = window.tightrope;
  if (!api?.clusterEnable) {
    return false;
  }
  await api.clusterEnable(config);
  return true;
}

export async function clusterDisableRequest(): Promise<boolean> {
  const api = window.tightrope;
  if (!api?.clusterDisable) {
    return false;
  }
  await api.clusterDisable();
  return true;
}

export async function addPeerRequest(address: string): Promise<boolean> {
  const api = window.tightrope;
  if (!api?.addPeer) {
    return false;
  }
  await api.addPeer(address);
  return true;
}

export async function removePeerRequest(siteId: string): Promise<boolean> {
  const api = window.tightrope;
  if (!api?.removePeer) {
    return false;
  }
  await api.removePeer(siteId);
  return true;
}

export async function triggerSyncRequest(): Promise<boolean> {
  const api = window.tightrope;
  if (!api?.triggerSync) {
    return false;
  }
  await api.triggerSync();
  return true;
}

export function onOauthDeepLinkRequest(listener: (event: OauthDeepLinkEvent) => void): (() => void) | null {
  const api = window.tightrope;
  if (!api?.onOauthDeepLink) {
    return null;
  }
  return api.onOauthDeepLink(listener);
}

export function onAboutOpenRequest(listener: () => void): (() => void) | null {
  const api = window.tightrope;
  if (!api?.onAboutOpen) {
    return null;
  }
  return api.onAboutOpen(listener);
}

export function onSyncEventRequest(listener: (event: SyncEvent) => void): (() => void) | null {
  const api = window.tightrope;
  if (!api?.onSyncEvent) {
    return null;
  }
  return api.onSyncEvent(listener);
}

export function platformRequest(): NodeJS.Platform | null {
  const api = window.tightrope;
  return api?.platform ?? null;
}

export async function windowCloseRequest(): Promise<boolean> {
  const api = window.tightrope;
  if (!api?.windowClose) {
    return false;
  }
  await api.windowClose();
  return true;
}

export async function windowMinimizeRequest(): Promise<boolean> {
  const api = window.tightrope;
  if (!api?.windowMinimize) {
    return false;
  }
  await api.windowMinimize();
  return true;
}

export async function windowToggleMaximizeRequest(): Promise<boolean> {
  const api = window.tightrope;
  if (!api?.windowToggleMaximize) {
    return false;
  }
  await api.windowToggleMaximize();
  return true;
}

export async function windowIsMaximizedRequest(): Promise<boolean | null> {
  const api = window.tightrope;
  if (!api?.windowIsMaximized) {
    return null;
  }
  return api.windowIsMaximized();
}

export function createTightropeService(): TightropeService {
  return {
    getAppMetaRequest,
    oauthStartRequest,
    oauthStatusRequest,
    oauthStopRequest,
    oauthRestartRequest,
    oauthCompleteRequest,
    oauthManualCallbackRequest,
    listAccountsRequest,
    listStickySessionsRequest,
    purgeStaleSessionsRequest,
    listRequestLogsRequest,
    listAccountTrafficRequest,
    backendStatusRequest,
    backendStartRequest,
    backendStopRequest,
    importAccountRequest,
    previewSqlImportRequest,
    pickSqlImportSourcePathRequest,
    applySqlImportRequest,
    pinAccountRequest,
    unpinAccountRequest,
    pauseAccountRequest,
    reactivateAccountRequest,
    deleteAccountRequest,
    refreshAccountUsageTelemetryRequest,
    refreshAccountTokenRequest,
    getSettingsRequest,
    updateSettingsRequest,
    changeDatabasePassphraseRequest,
    exportDatabaseRequest,
    listFirewallIpsRequest,
    addFirewallIpRequest,
    removeFirewallIpRequest,
    getClusterStatusRequest,
    clusterEnableRequest,
    clusterDisableRequest,
    addPeerRequest,
    removePeerRequest,
    triggerSyncRequest,
    onOauthDeepLinkRequest,
    onAboutOpenRequest,
    onSyncEventRequest,
    platformRequest,
    windowCloseRequest,
    windowMinimizeRequest,
    windowToggleMaximizeRequest,
    windowIsMaximizedRequest,
  };
}
