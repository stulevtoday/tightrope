import { useEffect, useRef, useState } from 'react';
import { createInitialRuntimeState, routingModesSeed } from '../data/seed';
import type {
  Account,
  AccountState,
  AddAccountStep,
  AppPage,
  AppRuntimeState,
  ClusterStatus,
  DashboardSettings,
  DashboardSettingsUpdate,
  FirewallIpEntry,
  FirewallMode,
  ManualCallbackResponse,
  OauthCompleteResponse,
  OauthDeepLinkEvent,
  OauthStartResponse,
  OauthStatusResponse,
  RuntimeAccount,
  RuntimeAccountTraffic,
  RuntimeAccountTrafficResponse,
  RuntimeRequestLog,
  RuntimeRequestLogsResponse,
  RouteRow,
  RoutingMode,
  SyncConflictResolution,
  ThemeMode,
  UpstreamStreamTransport,
} from '../shared/types';
import { publishStatusNotice, type StatusNoticeLevel } from './statusNotices';
import {
  buildListenerUrl,
  currentRouterState,
  deriveKpis,
  deriveMetrics,
  deterministicAccountDetailValues,
  ensureSelectedRouteId,
  filteredRows,
  formatNumber,
  modeLabel,
  nowStamp,
  paginateSessions,
  shouldScheduleAutoSync,
  selectRoutedAccountId,
  stableSparklinePercents,
} from './logic';
const SESSIONS_LIMIT = 10;
const DEFAULT_DEVICE_EXPIRES_SECONDS = 900;
const DEFAULT_OAUTH_CALLBACK_PORT = 1455;
const BROWSER_OAUTH_POLL_MS = 1000;
const OAUTH_DEEP_LINK_RETRY_MS = 250;
const OAUTH_DEEP_LINK_MAX_ATTEMPTS = 12;
const ACCOUNT_USAGE_REFRESH_RETRY_MS = 750;
const ACCOUNT_USAGE_REFRESH_MAX_ATTEMPTS = 4;
const REQUEST_LOGS_LIMIT = 250;
const REQUEST_LOGS_REFRESH_MS = 1000;
const TRAFFIC_WS_ENDPOINT = '/api/accounts/traffic/ws';
const TRAFFIC_WS_HOST = '127.0.0.1:2455';
const RUNTIME_HTTP_HOST = '127.0.0.1:2455';
const TRAFFIC_WS_RECONNECT_MS = 1200;
const TRAFFIC_ACTIVE_WINDOW_MS = 3000;
const TRAFFIC_CLOCK_TICK_MS = 200;
const TRAFFIC_SNAPSHOT_POLL_MS = 1000;
const TRAFFIC_FRAME_VERSION = 1;
const TRAFFIC_FRAME_BYTES = 50;

interface AccountTrafficFrame {
  accountId: string;
  upBytes: number;
  downBytes: number;
  lastUpAtMs: number;
  lastDownAtMs: number;
}

const EMPTY_ROUTE_ROW: RouteRow = {
  time: '--:--:--',
  id: '',
  model: '—',
  accountId: '',
  tokens: 0,
  latency: 0,
  status: 'ok',
  protocol: 'SSE',
  sessionId: '',
  sticky: false,
};

const defaultDashboardSettings: DashboardSettings = {
  theme: 'auto',
  stickyThreadsEnabled: false,
  upstreamStreamTransport: 'default',
  preferEarlierResetAccounts: false,
  routingStrategy: 'usage_weighted',
  openaiCacheAffinityMaxAgeSeconds: 300,
  importWithoutOverwrite: false,
  totpRequiredOnLogin: false,
  totpConfigured: false,
  apiKeyAuthEnabled: false,
  routingHeadroomWeightPrimary: 0.35,
  routingHeadroomWeightSecondary: 0.65,
  routingScoreAlpha: 0.3,
  routingScoreBeta: 0.25,
  routingScoreGamma: 0.2,
  routingScoreDelta: 0.2,
  routingScoreZeta: 0.05,
  routingScoreEta: 1,
  routingSuccessRateRho: 2,
  syncClusterName: 'default',
  syncSiteId: 1,
  syncPort: 9400,
  syncDiscoveryEnabled: true,
  syncIntervalSeconds: 5,
  syncConflictResolution: 'lww',
  syncJournalRetentionDays: 30,
  syncTlsEnabled: true,
};

const emptyClusterStatus: ClusterStatus = {
  enabled: false,
  site_id: '',
  cluster_name: '',
  role: 'standalone',
  term: 0,
  commit_index: 0,
  leader_id: null,
  peers: [],
  journal_entries: 0,
  pending_raft_entries: 0,
  last_sync_at: null,
};

function cloneRoutingModes(): RoutingMode[] {
  return routingModesSeed.map((mode) => ({
    ...mode,
    params: mode.params ? { ...mode.params } : undefined,
  }));
}

function makeSettingsUpdate(settings: DashboardSettings): DashboardSettingsUpdate {
  return {
    theme: settings.theme,
    stickyThreadsEnabled: settings.stickyThreadsEnabled,
    upstreamStreamTransport: settings.upstreamStreamTransport,
    preferEarlierResetAccounts: settings.preferEarlierResetAccounts,
    routingStrategy: settings.routingStrategy,
    openaiCacheAffinityMaxAgeSeconds: settings.openaiCacheAffinityMaxAgeSeconds,
    importWithoutOverwrite: settings.importWithoutOverwrite,
    totpRequiredOnLogin: settings.totpRequiredOnLogin,
    apiKeyAuthEnabled: settings.apiKeyAuthEnabled,
    routingHeadroomWeightPrimary: settings.routingHeadroomWeightPrimary,
    routingHeadroomWeightSecondary: settings.routingHeadroomWeightSecondary,
    routingScoreAlpha: settings.routingScoreAlpha,
    routingScoreBeta: settings.routingScoreBeta,
    routingScoreGamma: settings.routingScoreGamma,
    routingScoreDelta: settings.routingScoreDelta,
    routingScoreZeta: settings.routingScoreZeta,
    routingScoreEta: settings.routingScoreEta,
    routingSuccessRateRho: settings.routingSuccessRateRho,
    syncClusterName: settings.syncClusterName,
    syncSiteId: settings.syncSiteId,
    syncPort: settings.syncPort,
    syncDiscoveryEnabled: settings.syncDiscoveryEnabled,
    syncIntervalSeconds: settings.syncIntervalSeconds,
    syncConflictResolution: settings.syncConflictResolution,
    syncJournalRetentionDays: settings.syncJournalRetentionDays,
    syncTlsEnabled: settings.syncTlsEnabled,
  };
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

async function oauthStartRequest(forceMethod: 'browser' | 'device'): Promise<OauthStartResponse> {
  const api = window.tightrope;
  if (api?.oauthStart) {
    return api.oauthStart({ forceMethod });
  }
  return postJson<OauthStartResponse>('/api/oauth/start', { forceMethod });
}

async function oauthStatusRequest(): Promise<OauthStatusResponse> {
  const api = window.tightrope;
  if (api?.oauthStatus) {
    return api.oauthStatus();
  }
  return getJson<OauthStatusResponse>('/api/oauth/status');
}

async function oauthStopRequest(): Promise<OauthStatusResponse> {
  const api = window.tightrope;
  if (api?.oauthStop) {
    return api.oauthStop();
  }
  return postJson<OauthStatusResponse>('/api/oauth/stop', {});
}

async function oauthRestartRequest(): Promise<OauthStartResponse> {
  const api = window.tightrope;
  if (api?.oauthRestart) {
    return api.oauthRestart();
  }
  return postJson<OauthStartResponse>('/api/oauth/restart', {});
}

async function oauthCompleteRequest(payload: { deviceAuthId?: string; userCode?: string }): Promise<OauthCompleteResponse> {
  const api = window.tightrope;
  if (api?.oauthComplete) {
    return api.oauthComplete(payload);
  }
  return postJson<OauthCompleteResponse>('/api/oauth/complete', payload);
}

async function oauthManualCallbackRequest(callbackUrl: string): Promise<ManualCallbackResponse> {
  const api = window.tightrope;
  if (api?.oauthManualCallback) {
    return api.oauthManualCallback(callbackUrl);
  }
  return postJson<ManualCallbackResponse>('/api/oauth/manual-callback', { callbackUrl });
}

async function listAccountsRequest(): Promise<RuntimeAccount[]> {
  const api = window.tightrope;
  if (api?.listAccounts) {
    const response = await api.listAccounts();
    return Array.isArray(response.accounts) ? response.accounts : [];
  }
  const response = await getJson<{ accounts?: RuntimeAccount[] }>('/api/accounts');
  return Array.isArray(response.accounts) ? response.accounts : [];
}

async function listRequestLogsRequest(limit = REQUEST_LOGS_LIMIT, offset = 0): Promise<RuntimeRequestLog[]> {
  const api = window.tightrope;
  if (api?.listRequestLogs) {
    const response = await api.listRequestLogs({ limit, offset });
    return Array.isArray(response.logs) ? response.logs : [];
  }
  const response = await getJson<RuntimeRequestLogsResponse>(runtimeHttpUrl(`/api/logs?limit=${limit}&offset=${offset}`));
  return Array.isArray(response.logs) ? response.logs : [];
}

async function listAccountTrafficRequest(): Promise<RuntimeAccountTraffic[]> {
  const api = window.tightrope;
  if (api?.listAccountTraffic) {
    const response = await api.listAccountTraffic();
    return Array.isArray(response.accounts) ? response.accounts : [];
  }
  const response = await getJson<RuntimeAccountTrafficResponse>(runtimeHttpUrl('/api/accounts/traffic'));
  return Array.isArray(response.accounts) ? response.accounts : [];
}

async function importAccountRequest(email: string, provider: string): Promise<RuntimeAccount> {
  const api = window.tightrope;
  if (api?.importAccount) {
    return api.importAccount({ email, provider });
  }
  return postJson<RuntimeAccount>('/api/accounts/import', { email, provider });
}

async function pauseAccountRequest(accountId: string): Promise<void> {
  const api = window.tightrope;
  if (api?.pauseAccount) {
    await api.pauseAccount(accountId);
    return;
  }
  await postJson<{ status: string }>(`/api/accounts/${encodeURIComponent(accountId)}/pause`, {});
}

async function reactivateAccountRequest(accountId: string): Promise<void> {
  const api = window.tightrope;
  if (api?.reactivateAccount) {
    await api.reactivateAccount(accountId);
    return;
  }
  await postJson<{ status: string }>(`/api/accounts/${encodeURIComponent(accountId)}/reactivate`, {});
}

async function deleteAccountRequest(accountId: string): Promise<void> {
  const api = window.tightrope;
  if (api?.deleteAccount) {
    await api.deleteAccount(accountId);
    return;
  }
  const response = await fetch(`/api/accounts/${encodeURIComponent(accountId)}`, { method: 'DELETE' });
  await parseJsonResponse<{ status: string }>(response);
}

async function refreshAccountUsageTelemetryRequest(accountId: string): Promise<RuntimeAccount> {
  const api = window.tightrope;
  if (api?.refreshAccountUsageTelemetry) {
    return api.refreshAccountUsageTelemetry(accountId);
  }
  return postJson<RuntimeAccount>(`/api/accounts/${encodeURIComponent(accountId)}/refresh-usage`, {});
}

function mapAccountState(status: string): AccountState {
  if (
    status === 'active' ||
    status === 'paused' ||
    status === 'rate_limited' ||
    status === 'deactivated' ||
    status === 'quota_blocked'
  ) {
    return status;
  }
  return 'deactivated';
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => {
    setTimeout(resolve, ms);
  });
}

function mapRuntimePlan(planType: string | null | undefined): Account['plan'] {
  const normalized = typeof planType === 'string' ? planType.trim().toLowerCase() : '';
  if (normalized.includes('enterprise') || normalized.includes('business') || normalized.includes('team')) {
    return 'enterprise';
  }
  if (normalized.includes('free') || normalized.includes('guest')) {
    return 'free';
  }
  if (normalized.includes('plus') || normalized.includes('pro') || normalized.includes('premium') || normalized.includes('paid')) {
    return 'plus';
  }
  return 'free';
}

function runtimeNumber(value: unknown): number | null {
  if (typeof value !== 'number' || !Number.isFinite(value)) {
    return null;
  }
  return value;
}

function mapTransportProtocol(value: string | null | undefined): RouteRow['protocol'] {
  const normalized = typeof value === 'string' ? value.trim().toLowerCase() : '';
  if (normalized === 'websocket' || normalized === 'ws') {
    return 'WS';
  }
  if (normalized === 'compact') {
    return 'Compact';
  }
  if (normalized === 'transcribe') {
    return 'Transcribe';
  }
  return 'SSE';
}

function mapStatusBadge(statusCode: number, errorCode: string | null | undefined): RouteRow['status'] {
  if (statusCode >= 500) {
    return 'error';
  }
  if (statusCode >= 400 || (typeof errorCode === 'string' && errorCode.trim() !== '')) {
    return 'warn';
  }
  return 'ok';
}

function routeTimeLabel(requestedAt: string): string {
  const trimmed = requestedAt.trim();
  const date = new Date(trimmed.includes('T') ? trimmed : trimmed.replace(' ', 'T') + 'Z');
  if (!Number.isNaN(date.getTime())) {
    return date.toTimeString().slice(0, 8);
  }
  if (trimmed.length >= 8) {
    return trimmed.slice(-8);
  }
  return nowStamp();
}

function runtimeRequestLogToRouteRow(log: RuntimeRequestLog): RouteRow {
  const accountId = typeof log.accountId === 'string' ? log.accountId : '';
  const statusCode = Number.isFinite(log.statusCode) ? Math.trunc(log.statusCode) : 0;
  const model = typeof log.model === 'string' && log.model.trim() !== '' ? log.model.trim() : '—';
  const path = typeof log.path === 'string' ? log.path : '';
  const method = typeof log.method === 'string' ? log.method : '';
  const requestedAt = typeof log.requestedAt === 'string' ? log.requestedAt : '';
  const errorCode = typeof log.errorCode === 'string' ? log.errorCode : null;
  const transport = typeof log.transport === 'string' ? log.transport : null;
  const totalCost = Number.isFinite(log.totalCost) ? log.totalCost : 0;

  return {
    id: `log_${log.id}`,
    time: routeTimeLabel(requestedAt),
    model,
    accountId,
    tokens: 0,
    latency: 0,
    status: mapStatusBadge(statusCode, errorCode),
    protocol: mapTransportProtocol(transport),
    sessionId: path,
    sticky: false,
    method,
    path,
    statusCode,
    errorCode,
    requestedAt,
    totalCost,
  };
}

function trafficWsUrl(bindLabel: string | null | undefined): string {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const trimmedBind = (bindLabel ?? '').trim();
  const host = /^[A-Za-z0-9._-]+:\d+$/.test(trimmedBind) ? trimmedBind : TRAFFIC_WS_HOST;
  return `${protocol}//${host}${TRAFFIC_WS_ENDPOINT}`;
}

function bigintToNumber(value: bigint): number {
  const max = BigInt(Number.MAX_SAFE_INTEGER);
  return Number(value > max ? max : value);
}

function decodeTrafficFrame(data: ArrayBuffer): AccountTrafficFrame | null {
  if (data.byteLength !== TRAFFIC_FRAME_BYTES) {
    return null;
  }
  const view = new DataView(data);
  const version = view.getUint8(0);
  if (version !== TRAFFIC_FRAME_VERSION) {
    return null;
  }
  const accountId = bigintToNumber(view.getBigUint64(10, true)).toString();
  if (accountId.length === 0) {
    return null;
  }
  return {
    accountId,
    upBytes: bigintToNumber(view.getBigUint64(18, true)),
    downBytes: bigintToNumber(view.getBigUint64(26, true)),
    lastUpAtMs: bigintToNumber(view.getBigUint64(34, true)),
    lastDownAtMs: bigintToNumber(view.getBigUint64(42, true)),
  };
}

function runtimeAccountToUiAccount(record: RuntimeAccount): Account {
  const state = mapAccountState(record.status);
  const load = runtimeNumber(record.loadPercent);
  const quotaPrimary = runtimeNumber(record.quotaPrimaryPercent);
  const quotaSecondary = runtimeNumber(record.quotaSecondaryPercent);
  const inflight = runtimeNumber(record.inflight);
  const latency = runtimeNumber(record.latencyMs);
  const errorEwma = runtimeNumber(record.errorEwma);
  const stickyHit = runtimeNumber(record.stickyHitPercent);
  const routed24h = runtimeNumber(record.requests24h);
  const failovers = runtimeNumber(record.failovers24h);
  const costNorm = runtimeNumber(record.costNorm);
  const telemetryBacked = [
    load,
    quotaPrimary,
    quotaSecondary,
    inflight,
    latency,
    errorEwma,
    stickyHit,
    routed24h,
    failovers,
    costNorm,
  ].some((value) => value !== null);

  const capability = state === 'active';
  const cooldown = state !== 'active';
  const resolvedErrorEwma = errorEwma ?? 0;
  const health: Account['health'] = state === 'active' ? (resolvedErrorEwma < 0.15 ? 'healthy' : 'strained') : 'strained';
  const plan = mapRuntimePlan(record.planType);

  return {
    id: record.accountId,
    name: record.email,
    plan,
    health,
    state,
    inflight: inflight ?? 0,
    load: load ?? 0,
    latency: latency ?? 0,
    errorEwma: resolvedErrorEwma,
    cooldown,
    capability,
    costNorm: costNorm ?? 0,
    routed24h: routed24h ?? 0,
    stickyHit: stickyHit ?? 0,
    quotaPrimary: quotaPrimary ?? 0,
    quotaSecondary: quotaSecondary ?? 0,
    failovers: failovers ?? 0,
    note: record.provider || 'openai',
    telemetryBacked,
    trafficUpBytes: 0,
    trafficDownBytes: 0,
    trafficLastUpAtMs: 0,
    trafficLastDownAtMs: 0,
  };
}

function fallbackRouteAccount(accountId: string): Account {
  return runtimeAccountToUiAccount({
    accountId,
    email: `${accountId}@unassigned.local`,
    provider: 'openai',
    status: 'deactivated',
  });
}

function sanitizeEmailLocalPart(input: string): string {
  const lowered = input.toLowerCase();
  const sanitized = lowered.replace(/[^a-z0-9._-]/g, '-').replace(/-+/g, '-').replace(/^-+|-+$/g, '');
  if (sanitized.length > 0) {
    return sanitized.slice(0, 48);
  }
  return `account-${Date.now().toString(36)}`;
}

function oauthEmailFromHints(callbackUrl: string | null, fallbackHint: string): string {
  if (callbackUrl) {
    try {
      const parsed = new URL(callbackUrl);
      const maybeEmail = parsed.searchParams.get('email') ?? parsed.searchParams.get('user_email');
      if (maybeEmail && maybeEmail.includes('@')) {
        return maybeEmail;
      }
      const maybeCode = parsed.searchParams.get('code') ?? parsed.searchParams.get('state');
      if (maybeCode) {
        return `${sanitizeEmailLocalPart(maybeCode)}@openai.local`;
      }
    } catch {
      // ignore malformed callback URL and fallback below
    }
  }
  return `${sanitizeEmailLocalPart(fallbackHint)}@openai.local`;
}

function stringField(value: unknown): string | null {
  return typeof value === 'string' && value.trim() !== '' ? value.trim() : null;
}

function extractImportAccountPayload(raw: unknown): { email: string; provider: string } | null {
  if (!raw || typeof raw !== 'object') {
    return null;
  }
  const object = raw as Record<string, unknown>;
  const nestedAccount =
    object.account && typeof object.account === 'object' ? (object.account as Record<string, unknown>) : undefined;
  const nestedUser =
    object.user && typeof object.user === 'object' ? (object.user as Record<string, unknown>) : undefined;
  const nestedAccounts =
    Array.isArray(object.accounts) && object.accounts.length > 0 && typeof object.accounts[0] === 'object'
      ? (object.accounts[0] as Record<string, unknown>)
      : undefined;

  const email =
    stringField(object.email) ??
    stringField(object.accountEmail) ??
    stringField(nestedAccount?.email) ??
    stringField(nestedUser?.email) ??
    stringField(nestedAccounts?.email);
  const fallbackTokenSeed =
    stringField(object.refresh_token) ??
    stringField(object.access_token) ??
    stringField(object.token) ??
    stringField(nestedAccount?.refresh_token) ??
    stringField(nestedAccount?.access_token);
  const resolvedEmail =
    email ??
    (fallbackTokenSeed ? `${sanitizeEmailLocalPart(fallbackTokenSeed)}@openai.local` : null);
  if (!resolvedEmail) {
    return null;
  }
  const provider =
    stringField(object.provider) ??
    stringField(nestedAccount?.provider) ??
    stringField(nestedUser?.provider) ??
    stringField(nestedAccounts?.provider) ??
    'openai';
  return { email: resolvedEmail, provider };
}

function readFileText(file: File): Promise<string> {
  const textMethod = (file as File & { text?: () => Promise<string> }).text;
  if (typeof textMethod === 'function') {
    return textMethod.call(file);
  }
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onerror = () => reject(new Error('Failed to read import file'));
    reader.onload = () => resolve(typeof reader.result === 'string' ? reader.result : '');
    reader.readAsText(file);
  });
}

function oauthStateToken(authorizationUrl: string): string | null {
  if (!isValidOAuthAuthorizationUrl(authorizationUrl)) {
    return null;
  }
  try {
    const url = new URL(authorizationUrl);
    return url.searchParams.get('state');
  } catch {
    return null;
  }
}

function isValidOAuthAuthorizationUrl(urlValue: string | null | undefined): urlValue is string {
  if (typeof urlValue !== 'string') {
    return false;
  }
  const trimmed = urlValue.trim();
  if (!trimmed || trimmed.includes('...')) {
    return false;
  }
  try {
    const parsed = new URL(trimmed);
    return (
      parsed.pathname === '/oauth/authorize' &&
      parsed.searchParams.get('response_type') === 'code' &&
      !!parsed.searchParams.get('state')
    );
  } catch {
    return false;
  }
}

async function writeClipboardText(value: string): Promise<void> {
  if (typeof navigator !== 'undefined' && navigator.clipboard?.writeText) {
    await navigator.clipboard.writeText(value);
    return;
  }

  if (typeof document === 'undefined' || typeof document.execCommand !== 'function') {
    throw new Error('Clipboard API unavailable');
  }

  const element = document.createElement('textarea');
  element.value = value;
  element.setAttribute('readonly', 'true');
  element.style.position = 'fixed';
  element.style.opacity = '0';
  element.style.left = '-9999px';
  document.body.appendChild(element);
  element.select();
  const copied = document.execCommand('copy');
  document.body.removeChild(element);
  if (!copied) {
    throw new Error('Clipboard write failed');
  }
}

function callbackParts(callbackUrl: string): { callbackPath: string; listenerPort: number; listenerUrl: string } {
  try {
    const parsed = new URL(callbackUrl);
    const callbackPath = parsed.pathname || '/auth/callback';
    const listenerPort = parsed.port ? Number(parsed.port) : parsed.protocol === 'https:' ? 443 : 80;
    return {
      callbackPath,
      listenerPort: Number.isFinite(listenerPort) && listenerPort > 0 ? listenerPort : DEFAULT_OAUTH_CALLBACK_PORT,
      listenerUrl: parsed.toString(),
    };
  } catch {
    return {
      callbackPath: '/auth/callback',
      listenerPort: DEFAULT_OAUTH_CALLBACK_PORT,
      listenerUrl: callbackUrl,
    };
  }
}

function resetAddAccountTransientState(state: AppRuntimeState): AppRuntimeState {
  return {
    ...state,
    addAccountStep: 'stepMethod',
    selectedFileName: '',
    manualCallback: '',
    deviceCountdownSeconds: 900,
    copyAuthLabel: 'Copy',
    copyDeviceLabel: 'Copy',
    addAccountError: 'Something went wrong.',
  };
}

export function useTightropeState() {
  const [state, setState] = useState<AppRuntimeState>(() => createInitialRuntimeState());
  const [accounts, setAccounts] = useState<Account[]>([]);
  const [routingModes, setRoutingModes] = useState<RoutingMode[]>(() => cloneRoutingModes());
  const [dashboardSettings, setDashboardSettings] = useState<DashboardSettings>({ ...defaultDashboardSettings });
  const [firewallMode, setFirewallMode] = useState<FirewallMode>('allow_all');
  const [firewallEntries, setFirewallEntries] = useState<FirewallIpEntry[]>([]);
  const [firewallDraftIpAddress, setFirewallDraftIpAddress] = useState('');
  const [clusterStatus, setClusterStatus] = useState<ClusterStatus>({ ...emptyClusterStatus });
  const [manualPeerAddress, setManualPeerAddress] = useState('');
  const [refreshingAccountTelemetryId, setRefreshingAccountTelemetryId] = useState<string | null>(null);
  const [trafficClockMs, setTrafficClockMs] = useState<number>(() => Date.now());
  const dashboardSettingsRef = useRef<DashboardSettings>(defaultDashboardSettings);
  const deviceTimerRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const deviceSuccessRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const oauthPollRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const browserOauthPollRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const oauthStartInFlightRef = useRef<Promise<string | null> | null>(null);
  const oauthDeepLinkFinalizeInFlightRef = useRef(false);
  const oauthAccountBaselineIdsRef = useRef<Set<string>>(new Set());
  const copyTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const selectedImportFileRef = useRef<File | null>(null);
  const trafficWsRef = useRef<WebSocket | null>(null);
  const trafficReconnectTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const trafficClockTimerRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const pendingTrafficByAccountRef = useRef<Map<string, AccountTrafficFrame>>(new Map());
  const requestLogRefreshInFlightRef = useRef(false);
  const trafficSnapshotRefreshInFlightRef = useRef(false);

  const metrics = deriveMetrics(
    accounts,
    state.scoringModel,
    state.routingMode,
    state.roundRobinCursor,
    routingModes,
  );
  const visibleRows = filteredRows(state.rows, accounts, state.selectedAccountId, state.searchQuery);
  const ensuredRouteId = ensureSelectedRouteId(visibleRows, state.selectedRouteId);
  const selectedRoute = state.rows.find((route) => route.id === ensuredRouteId) ?? state.rows[0] ?? EMPTY_ROUTE_ROW;
  const routedAccountId =
    selectedRoute.accountId && accounts.some((account) => account.id === selectedRoute.accountId)
      ? selectedRoute.accountId
      : selectRoutedAccountId(accounts, metrics);
  const activeRoutingMode = routingModes.find((mode) => mode.id === state.routingMode) ?? routingModes[0];
  const sessionsView = paginateSessions(state.sessions, state.sessionsKindFilter, state.sessionsOffset, SESSIONS_LIMIT);
  const accountFilterQuery = state.accountSearchQuery.trim().toLowerCase();
  const filteredAccounts = accounts.filter((account) => {
    if (state.accountStatusFilter && account.state !== state.accountStatusFilter) return false;
    if (!accountFilterQuery) return true;
    return account.name.toLowerCase().includes(accountFilterQuery) || account.id.toLowerCase().includes(accountFilterQuery);
  });
  const selectedAccountDetail = accounts.find((account) => account.id === state.selectedAccountDetailId) ?? null;
  const isRefreshingSelectedAccountTelemetry =
    selectedAccountDetail !== null && refreshingAccountTelemetryId === selectedAccountDetail.id;
  const canScheduleAutoSync = shouldScheduleAutoSync(clusterStatus, dashboardSettings.syncIntervalSeconds);

  useEffect(() => {
    if (ensuredRouteId !== state.selectedRouteId) {
      setState((previous) => ({ ...previous, selectedRouteId: ensuredRouteId }));
    }
  }, [ensuredRouteId, state.selectedRouteId]);

  useEffect(() => {
    setState((previous) => {
      const hasSelectedRoutingAccount = previous.selectedAccountId
        ? accounts.some((account) => account.id === previous.selectedAccountId)
        : false;
      const nextSelectedAccountId = hasSelectedRoutingAccount ? previous.selectedAccountId : '';
      const hasSelectedDetail = previous.selectedAccountDetailId
        ? accounts.some((account) => account.id === previous.selectedAccountDetailId)
        : false;
      const nextSelectedDetailId = hasSelectedDetail ? previous.selectedAccountDetailId : null;
      if (nextSelectedAccountId === previous.selectedAccountId && nextSelectedDetailId === previous.selectedAccountDetailId) {
        return previous;
      }
      return {
        ...previous,
        selectedAccountId: nextSelectedAccountId,
        selectedAccountDetailId: nextSelectedDetailId,
      };
    });
  }, [accounts]);

  useEffect(() => {
    const resolvedTheme =
      state.theme === 'auto' ? (window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light') : state.theme;
    document.documentElement.setAttribute('data-theme', resolvedTheme);
  }, [state.theme]);

  useEffect(() => {
    if (trafficClockTimerRef.current) {
      clearInterval(trafficClockTimerRef.current);
    }
    trafficClockTimerRef.current = setInterval(() => {
      setTrafficClockMs(Date.now());
    }, TRAFFIC_CLOCK_TICK_MS);
    return () => {
      if (trafficClockTimerRef.current) {
        clearInterval(trafficClockTimerRef.current);
        trafficClockTimerRef.current = null;
      }
    };
  }, []);

  useEffect(
    () => () => {
      if (deviceTimerRef.current) clearInterval(deviceTimerRef.current);
      if (deviceSuccessRef.current) clearTimeout(deviceSuccessRef.current);
      if (oauthPollRef.current) clearInterval(oauthPollRef.current);
      if (browserOauthPollRef.current) clearInterval(browserOauthPollRef.current);
      if (copyTimerRef.current) clearTimeout(copyTimerRef.current);
      if (trafficClockTimerRef.current) clearInterval(trafficClockTimerRef.current);
      if (trafficReconnectTimerRef.current) clearTimeout(trafficReconnectTimerRef.current);
      if (trafficWsRef.current) {
        trafficWsRef.current.close();
        trafficWsRef.current = null;
      }
    },
    [],
  );

  useEffect(() => {
    let cancelled = false;
    async function bootstrapSettings(): Promise<void> {
      try {
        await refreshAccountsFromNative();
      } catch (error) {
        if (!cancelled) {
          const message = error instanceof Error ? error.message : 'Failed to load accounts';
          pushRuntimeEvent(message, 'warn');
        }
      }
      try {
        await refreshAccountTrafficSnapshot();
      } catch {
        // websocket stream will continue to deliver updates
      }
      try {
        await refreshRequestLogs();
      } catch (error) {
        if (!cancelled) {
          const message = error instanceof Error ? error.message : 'Failed to load request logs';
          pushRuntimeEvent(message, 'warn');
        }
      }
      try {
        await refreshDashboardSettingsFromNative();
      } catch (error) {
        if (!cancelled) {
          const message = error instanceof Error ? error.message : 'Failed to load settings';
          pushRuntimeEvent(message, 'warn');
        }
      }
      try {
        await refreshFirewallIps();
      } catch (error) {
        if (!cancelled) {
          const message = error instanceof Error ? error.message : 'Failed to load firewall allowlist';
          pushRuntimeEvent(message, 'warn');
        }
      }
      try {
        await refreshClusterState();
      } catch {
        // cluster can be disabled
      }
      try {
        const oauthStatus = await oauthStatusRequest();
        if (cancelled) {
          return;
        }
        applyOauthStatus(oauthStatus);
        if (!isValidOAuthAuthorizationUrl(oauthStatus.authorizationUrl)) {
          await createListenerUrl();
        }
      } catch (error) {
        if (!cancelled) {
          const message = error instanceof Error ? error.message : 'Failed to initialize OAuth state';
          pushRuntimeEvent(message, 'warn');
        }
      }
    }
    void bootstrapSettings();
    return () => {
      cancelled = true;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    const handle = setInterval(() => {
      void refreshClusterState().catch(() => {
        // cluster polling errors are transient while disabled/restarting
      });
    }, 2000);
    return () => clearInterval(handle);
  }, []);

  useEffect(() => {
    const handle = setInterval(() => {
      void refreshRequestLogs().catch(() => {
        // request log polling failures are transient while runtime restarts
      });
    }, REQUEST_LOGS_REFRESH_MS);
    return () => clearInterval(handle);
  }, []);

  useEffect(() => {
    const handle = setInterval(() => {
      void refreshAccountTrafficSnapshot().catch(() => {
        // websocket remains primary, poll keeps traffic indicators alive if ws is unavailable
      });
    }, TRAFFIC_SNAPSHOT_POLL_MS);
    return () => clearInterval(handle);
  }, []);

  useEffect(() => {
    const api = window.tightrope;
    if (!api?.triggerSync || !canScheduleAutoSync) {
      return;
    }
    const handle = setInterval(() => {
      void api
        .triggerSync()
        .then(() => refreshClusterState())
        .catch((error) => {
          const message = error instanceof Error ? error.message : 'Scheduled sync trigger failed';
          pushRuntimeEvent(message, 'warn');
        });
    }, dashboardSettings.syncIntervalSeconds * 1000);
    return () => clearInterval(handle);
  }, [canScheduleAutoSync, dashboardSettings.syncIntervalSeconds]);

  useEffect(() => {
    const api = window.tightrope;
    if (!api?.onOauthDeepLink) {
      return;
    }
    const unsubscribe = api.onOauthDeepLink((event: OauthDeepLinkEvent) => {
      pushRuntimeEvent(`oauth deep-link received: ${event.kind}`);
      if (event.kind === 'success') {
        void completeBrowserOauthFromDeepLink(event.url);
        return;
      }
      startBrowserOauthPolling();
    });
    return () => {
      unsubscribe();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    if (typeof WebSocket === 'undefined') {
      return;
    }

    let disposed = false;
    const clearReconnectTimer = () => {
      if (trafficReconnectTimerRef.current) {
        clearTimeout(trafficReconnectTimerRef.current);
        trafficReconnectTimerRef.current = null;
      }
    };
    const scheduleReconnect = () => {
      if (disposed) {
        return;
      }
      clearReconnectTimer();
      trafficReconnectTimerRef.current = setTimeout(() => {
        connect();
      }, TRAFFIC_WS_RECONNECT_MS);
    };
    const applyFrameFromBuffer = (buffer: ArrayBuffer) => {
      const frame = decodeTrafficFrame(buffer);
      if (!frame) {
        return;
      }
      applyTrafficFrame(frame);
    };
    const connect = () => {
      if (disposed) {
        return;
      }
      let ws: WebSocket;
      try {
        ws = new WebSocket(trafficWsUrl(state.runtimeState.bind));
      } catch {
        scheduleReconnect();
        return;
      }
      ws.binaryType = 'arraybuffer';
      trafficWsRef.current = ws;
      ws.onopen = () => {
        if (disposed) {
          return;
        }
        ws.send('snapshot');
        void refreshAccountTrafficSnapshot().catch(() => {
          // websocket snapshot message remains primary source
        });
      };
      ws.onmessage = (event: MessageEvent) => {
        if (disposed) {
          return;
        }
        const data = event.data;
        if (data instanceof ArrayBuffer) {
          applyFrameFromBuffer(data);
          return;
        }
        if (data instanceof Blob) {
          void data.arrayBuffer().then((buffer) => {
            if (!disposed) {
              applyFrameFromBuffer(buffer);
            }
          });
        }
      };
      ws.onclose = () => {
        if (trafficWsRef.current === ws) {
          trafficWsRef.current = null;
        }
        scheduleReconnect();
      };
      ws.onerror = () => {
        ws.close();
      };
    };

    connect();
    return () => {
      disposed = true;
      clearReconnectTimer();
      if (trafficWsRef.current) {
        trafficWsRef.current.close();
        trafficWsRef.current = null;
      }
    };
  }, [state.runtimeState.bind]);

  function pushRuntimeEvent(text: string, level: StatusNoticeLevel = 'info'): void {
    setState((previous) => ({
      ...previous,
      runtimeState: {
        ...previous.runtimeState,
        events: [`${nowStamp()} ${text}`, ...previous.runtimeState.events].slice(0, 12),
      },
    }));
    publishStatusNotice({ message: text, level });
  }

  function withUpdatedAuth(stateValue: AppRuntimeState, callbackPath = stateValue.authState.callbackPath): AppRuntimeState {
    const path = callbackPath.startsWith('/') ? callbackPath : `/${callbackPath}`;
    return {
      ...stateValue,
      authState: {
        ...stateValue.authState,
        callbackPath: path,
        listenerUrl: buildListenerUrl(stateValue.authState.listenerPort, path),
      },
    };
  }

  function captureOauthAccountBaseline(): void {
    oauthAccountBaselineIdsRef.current = new Set(accounts.map((account) => account.id));
  }

  function stopBrowserOauthPolling(): void {
    if (browserOauthPollRef.current) {
      clearInterval(browserOauthPollRef.current);
      browserOauthPollRef.current = null;
    }
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
    options?: { autoClose?: boolean; requireAccountVisible?: boolean },
  ): Promise<void> {
    try {
      const runtimeAccounts = await refreshAccountsFromNative();
      if (options?.requireAccountVisible && runtimeAccounts.length === 0) {
        throw new Error('OAuth completed, but no account is visible yet. Please retry.');
      }
      const selected = selectOauthImportedAccount(runtimeAccounts, callbackUrl, fallbackHint);
      const fallbackEmail = oauthEmailFromHints(callbackUrl, fallbackHint);
      if (options?.autoClose) {
        setState((previous) => resetAddAccountTransientState({ ...previous, addAccountOpen: false }));
      } else {
        setState((previous) => ({
          ...previous,
          addAccountStep: 'stepSuccess',
          successEmail: selected?.email ?? fallbackEmail,
          successPlan: selected?.provider || 'openai',
          addAccountError: 'Something went wrong.',
        }));
      }
      if (selected) {
        setState((previous) => ({ ...previous, selectedAccountDetailId: selected.accountId }));
      }
      pushRuntimeEvent(successEvent, 'success');
      if (selected) {
        void refreshUsageTelemetryAfterAccountAdd(selected.accountId, selected.email);
      }
    } catch (error) {
      const message = error instanceof Error ? error.message : 'OAuth succeeded but account refresh failed';
      setState((previous) => ({
        ...previous,
        addAccountStep: 'stepError',
        addAccountError: message,
      }));
      pushRuntimeEvent(message, 'warn');
    }
  }

  async function completeBrowserOauthFromDeepLink(deepLinkUrl: string): Promise<void> {
    if (oauthDeepLinkFinalizeInFlightRef.current) {
      return;
    }
    oauthDeepLinkFinalizeInFlightRef.current = true;
    stopBrowserOauthPolling();
    try {
      let callbackUrl: string | null = null;
      let lastError: string | null = null;

      for (let attempt = 0; attempt < OAUTH_DEEP_LINK_MAX_ATTEMPTS; attempt += 1) {
        try {
          const status = await oauthStatusRequest();
          applyOauthStatus(status);
          callbackUrl = status.callbackUrl ?? callbackUrl;
          if (status.status === 'error') {
            throw new Error(status.errorMessage ?? 'Browser OAuth failed');
          }
          if (status.status === 'success') {
            await finalizeOauthAccountSuccess(
              callbackUrl,
              deepLinkUrl,
              'browser oauth completed and account imported',
              { autoClose: true, requireAccountVisible: true },
            );
            return;
          }
        } catch (error) {
          lastError = error instanceof Error ? error.message : 'Failed to verify OAuth status after callback';
        }
        await sleep(OAUTH_DEEP_LINK_RETRY_MS);
      }

      const message = lastError ?? 'OAuth callback was received, but completion was not confirmed';
      setState((previous) => ({
        ...previous,
        addAccountStep: 'stepError',
        addAccountError: message,
      }));
      pushRuntimeEvent(message, 'warn');
    } finally {
      oauthDeepLinkFinalizeInFlightRef.current = false;
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

  function applyDashboardSettings(nextSettings: DashboardSettings): void {
    dashboardSettingsRef.current = nextSettings;
    setDashboardSettings(nextSettings);
    setRoutingModes((previous) =>
      previous.map((mode) =>
        mode.id === 'success_rate_weighted'
          ? { ...mode, params: { ...(mode.params ?? {}), rho: nextSettings.routingSuccessRateRho } }
          : mode,
      ),
    );
    setState((previous) => ({
      ...previous,
      theme: nextSettings.theme,
      routingMode: nextSettings.routingStrategy,
      scoringModel: {
        ...previous.scoringModel,
        wp: nextSettings.routingHeadroomWeightPrimary,
        ws: nextSettings.routingHeadroomWeightSecondary,
        alpha: nextSettings.routingScoreAlpha,
        beta: nextSettings.routingScoreBeta,
        gamma: nextSettings.routingScoreGamma,
        delta: nextSettings.routingScoreDelta,
        zeta: nextSettings.routingScoreZeta,
        eta: nextSettings.routingScoreEta,
      },
    }));
  }

  async function refreshAccountsFromNative(): Promise<RuntimeAccount[]> {
    const runtimeAccounts = await listAccountsRequest();
    setAccounts((previous) => {
      const previousById = new Map(previous.map((account) => [account.id, account]));
      const next = runtimeAccounts.map((record) => {
        const mapped = runtimeAccountToUiAccount(record);
        const existing = previousById.get(mapped.id);
        if (!existing) {
          return mapped;
        }
        return {
          ...mapped,
          trafficUpBytes: existing.trafficUpBytes ?? 0,
          trafficDownBytes: existing.trafficDownBytes ?? 0,
          trafficLastUpAtMs: existing.trafficLastUpAtMs ?? 0,
          trafficLastDownAtMs: existing.trafficLastDownAtMs ?? 0,
        };
      });

      if (pendingTrafficByAccountRef.current.size > 0) {
        for (let i = 0; i < next.length; i += 1) {
          const pending = pendingTrafficByAccountRef.current.get(next[i].id);
          if (!pending) {
            continue;
          }
          next[i] = {
            ...next[i],
            trafficUpBytes: pending.upBytes,
            trafficDownBytes: pending.downBytes,
            trafficLastUpAtMs: pending.lastUpAtMs,
            trafficLastDownAtMs: pending.lastDownAtMs,
          };
          pendingTrafficByAccountRef.current.delete(next[i].id);
        }
      }
      return next;
    });
    return runtimeAccounts;
  }

  function applyRuntimeAccountPatch(record: RuntimeAccount): void {
    const patch = runtimeAccountToUiAccount(record);
    setAccounts((previous) => {
      const index = previous.findIndex((account) => account.id === patch.id);
      if (index < 0) {
        return previous;
      }
      const next = previous.slice();
      const existing = next[index];
      next[index] = {
        ...patch,
        trafficUpBytes: existing.trafficUpBytes ?? 0,
        trafficDownBytes: existing.trafficDownBytes ?? 0,
        trafficLastUpAtMs: existing.trafficLastUpAtMs ?? 0,
        trafficLastDownAtMs: existing.trafficLastDownAtMs ?? 0,
      };
      return next;
    });
  }

  function applyTrafficFrame(frame: AccountTrafficFrame): void {
    setAccounts((previous) => {
      const index = previous.findIndex((account) => account.id === frame.accountId);
      if (index < 0) {
        pendingTrafficByAccountRef.current.set(frame.accountId, frame);
        return previous;
      }
      const current = previous[index];
      if (
        (current.trafficUpBytes ?? 0) === frame.upBytes &&
        (current.trafficDownBytes ?? 0) === frame.downBytes &&
        (current.trafficLastUpAtMs ?? 0) === frame.lastUpAtMs &&
        (current.trafficLastDownAtMs ?? 0) === frame.lastDownAtMs
      ) {
        return previous;
      }
      const next = previous.slice();
      next[index] = {
        ...current,
        trafficUpBytes: frame.upBytes,
        trafficDownBytes: frame.downBytes,
        trafficLastUpAtMs: frame.lastUpAtMs,
        trafficLastDownAtMs: frame.lastDownAtMs,
      };
      return next;
    });
  }

  function applyTrafficSnapshot(snapshot: RuntimeAccountTraffic[]): void {
    for (const account of snapshot) {
      applyTrafficFrame({
        accountId: account.accountId,
        upBytes: account.upBytes,
        downBytes: account.downBytes,
        lastUpAtMs: account.lastUpAtMs,
        lastDownAtMs: account.lastDownAtMs,
      });
    }
  }

  async function refreshRequestLogs(): Promise<void> {
    if (requestLogRefreshInFlightRef.current) {
      return;
    }
    requestLogRefreshInFlightRef.current = true;
    try {
      const logs = await listRequestLogsRequest(REQUEST_LOGS_LIMIT, 0);
      const rows = logs.map(runtimeRequestLogToRouteRow);
      setState((previous) => ({
        ...previous,
        rows,
        drawerRowId: previous.drawerRowId && rows.some((row) => row.id === previous.drawerRowId) ? previous.drawerRowId : null,
      }));
    } finally {
      requestLogRefreshInFlightRef.current = false;
    }
  }

  async function refreshAccountTrafficSnapshot(): Promise<void> {
    if (trafficSnapshotRefreshInFlightRef.current) {
      return;
    }
    trafficSnapshotRefreshInFlightRef.current = true;
    try {
      const accountsSnapshot = await listAccountTrafficRequest();
      applyTrafficSnapshot(accountsSnapshot);
    } finally {
      trafficSnapshotRefreshInFlightRef.current = false;
    }
  }

  async function refreshUsageTelemetryAfterAccountAdd(accountId: string, accountName: string): Promise<void> {
    let lastMessage = 'Failed to refresh usage telemetry';
    for (let attempt = 0; attempt < ACCOUNT_USAGE_REFRESH_MAX_ATTEMPTS; attempt += 1) {
      try {
        const refreshed = await refreshAccountUsageTelemetryRequest(accountId);
        applyRuntimeAccountPatch(refreshed);
        pushRuntimeEvent(`usage telemetry refreshed: ${accountName}`, 'success');
        return;
      } catch (error) {
        lastMessage = error instanceof Error ? error.message : 'Failed to refresh usage telemetry';
      }
      if (attempt + 1 < ACCOUNT_USAGE_REFRESH_MAX_ATTEMPTS) {
        await sleep(ACCOUNT_USAGE_REFRESH_RETRY_MS);
      }
    }
    pushRuntimeEvent(`account added, but usage telemetry is unavailable: ${lastMessage}`, 'warn');
  }

  async function refreshDashboardSettingsFromNative(): Promise<void> {
    const api = window.tightrope;
    if (!api?.getSettings) return;
    const nextSettings = await api.getSettings();
    if (!nextSettings.theme) {
      nextSettings.theme = state.theme;
    }
    if (!routingModes.some((mode) => mode.id === nextSettings.routingStrategy)) {
      nextSettings.routingStrategy = state.routingMode;
    }
    applyDashboardSettings(nextSettings);
  }

  async function persistDashboardSettingsPatch(patch: DashboardSettingsUpdate): Promise<void> {
    const api = window.tightrope;
    if (!api?.updateSettings) return;
    const nextSettings = {
      ...dashboardSettingsRef.current,
      ...patch,
    };
    applyDashboardSettings(nextSettings);
    try {
      const updated = await api.updateSettings(makeSettingsUpdate(nextSettings));
      applyDashboardSettings(updated);
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to save settings';
      pushRuntimeEvent(message, 'warn');
      try {
        await refreshDashboardSettingsFromNative();
      } catch {
        // keep optimistic value if refresh fails
      }
    }
  }

  async function refreshFirewallIps(): Promise<void> {
    const api = window.tightrope;
    if (!api?.listFirewallIps) return;
    const response = await api.listFirewallIps();
    setFirewallMode(response.mode);
    setFirewallEntries(response.entries);
  }

  async function refreshClusterState(): Promise<void> {
    const api = window.tightrope;
    if (!api?.getClusterStatus) return;
    const status = await api.getClusterStatus();
    setClusterStatus(status);
  }

  function applyOauthStatus(status: OauthStatusResponse): void {
    setState((previous) => {
      const callbackUrl = status.callbackUrl ?? previous.authState.listenerUrl;
      const parts = callbackParts(callbackUrl);
      const listenerRunning = status.listenerRunning ?? (status.status === 'pending');
      const nextAuthorizationUrl = isValidOAuthAuthorizationUrl(status.authorizationUrl)
        ? status.authorizationUrl
        : previous.browserAuthUrl;
      return {
        ...previous,
        authState: {
          ...previous.authState,
          callbackPath: parts.callbackPath,
          listenerPort: parts.listenerPort,
          listenerUrl: parts.listenerUrl,
          listenerRunning,
          initStatus: status.status,
          lastResponse: status.errorMessage ?? previous.authState.lastResponse,
        },
        browserAuthUrl: nextAuthorizationUrl,
      };
    });
  }

  function setCurrentPage(page: AppPage): void {
    setState((previous) => ({ ...previous, currentPage: page }));
  }

  function setSearchQuery(searchQuery: string): void {
    setState((previous) => ({ ...previous, searchQuery }));
  }

  function setSelectedAccountId(accountId: string): void {
    setState((previous) => ({ ...previous, selectedAccountId: accountId }));
  }

  function setSelectedRoute(route: RouteRow): void {
    setState((previous) => ({
      ...previous,
      selectedRouteId: route.id,
      selectedAccountId: route.accountId,
      inspectorOpen: true,
    }));
  }

  function setRoutingMode(nextMode: string): void {
    if (!routingModes.some((mode) => mode.id === nextMode)) return;
    const accountCount = Math.max(1, accounts.length);
    setState((previous) => {
      const next = {
        ...previous,
        routingMode: nextMode,
        roundRobinCursor:
          nextMode === 'round_robin' ? (previous.roundRobinCursor + 1) % accountCount : previous.roundRobinCursor,
      };
      return next;
    });
    pushRuntimeEvent(`routing policy set to ${modeLabel(nextMode, routingModes)}`);
    void persistDashboardSettingsPatch({ routingStrategy: nextMode });
  }

  function setRuntimeAction(action: 'start' | 'restart' | 'stop'): void {
    setState((previous) => {
      if (action === 'start') {
        return {
          ...previous,
          runtimeState: {
            ...previous.runtimeState,
            backend: 'running',
            health: 'ok',
          },
        };
      }
      if (action === 'restart') {
        return {
          ...previous,
          runtimeState: {
            ...previous.runtimeState,
            backend: 'running',
            health: 'ok',
            pausedRoutes: false,
          },
        };
      }
      return {
        ...previous,
        runtimeState: {
          ...previous.runtimeState,
          backend: 'stopped',
          health: 'warn',
          pausedRoutes: false,
        },
      };
    });
    if (action === 'start') pushRuntimeEvent('backend started', 'success');
    if (action === 'restart') pushRuntimeEvent('backend restarted', 'success');
    if (action === 'stop') pushRuntimeEvent('backend stopped', 'warn');
  }

  function toggleRoutePause(): void {
    if (state.runtimeState.backend !== 'running') {
      pushRuntimeEvent('pause ignored: backend is stopped', 'warn');
      return;
    }
    setState((previous) => ({
      ...previous,
      runtimeState: {
        ...previous.runtimeState,
        pausedRoutes: !previous.runtimeState.pausedRoutes,
      },
    }));
    pushRuntimeEvent(state.runtimeState.pausedRoutes ? 'new routes resumed' : 'new routes paused');
  }

  function toggleAutoRestart(): void {
    setState((previous) => ({
      ...previous,
      runtimeState: {
        ...previous.runtimeState,
        autoRestart: !previous.runtimeState.autoRestart,
      },
    }));
    pushRuntimeEvent(state.runtimeState.autoRestart ? 'auto-restart disabled' : 'auto-restart enabled');
  }

  async function createListenerUrl(): Promise<string | null> {
    if (oauthStartInFlightRef.current) {
      return oauthStartInFlightRef.current;
    }

    const pending = (async () => {
      try {
        const response = await oauthStartRequest('browser');
        const callbackUrl = response.callbackUrl ?? buildListenerUrl(DEFAULT_OAUTH_CALLBACK_PORT, '/auth/callback');
        const parts = callbackParts(callbackUrl);
        const authorizationUrl = isValidOAuthAuthorizationUrl(response.authorizationUrl) ? response.authorizationUrl : null;
        setState((previous) => ({
          ...previous,
          browserAuthUrl: authorizationUrl ?? previous.browserAuthUrl,
          authState: {
            ...previous.authState,
            callbackPath: parts.callbackPath,
            listenerPort: parts.listenerPort,
            listenerUrl: parts.listenerUrl,
            listenerRunning: true,
            initStatus: 'pending',
            lastResponse: previous.authState.lastResponse,
          },
        }));
        pushRuntimeEvent(`callback URL generated: ${callbackUrl}`);
        return authorizationUrl;
      } catch (error) {
        const message = error instanceof Error ? error.message : 'Failed to start OAuth browser flow';
        setState((previous) => ({
          ...previous,
          authState: {
            ...previous.authState,
            initStatus: 'error',
            lastResponse: message,
          },
        }));
        pushRuntimeEvent(message, 'warn');
        return null;
      } finally {
        oauthStartInFlightRef.current = null;
      }
    })();

    oauthStartInFlightRef.current = pending;
    return pending;
  }

  async function pollBrowserOauthStatus(): Promise<void> {
    try {
      const status = await oauthStatusRequest();
      if (!status || typeof status.status !== 'string') {
        return;
      }
      applyOauthStatus(status);
      if (status.status === 'success') {
        stopBrowserOauthPolling();
        await finalizeOauthAccountSuccess(
          status.callbackUrl ?? null,
          `oauth-${Date.now().toString(36)}`,
          'browser oauth completed and account imported',
        );
        return;
      }
      if (status.status === 'error') {
        stopBrowserOauthPolling();
        const message = status.errorMessage ?? 'Browser OAuth failed';
        setState((previous) => ({
          ...previous,
          addAccountStep: 'stepError',
          addAccountError: message,
        }));
        pushRuntimeEvent(message, 'warn');
        return;
      }
      if (status.status === 'idle' || status.status === 'stopped') {
        stopBrowserOauthPolling();
      }
    } catch {
      // keep polling while listener is active
    }
  }

  function startBrowserOauthPolling(): void {
    stopBrowserOauthPolling();
    void pollBrowserOauthStatus();
    browserOauthPollRef.current = setInterval(() => {
      void pollBrowserOauthStatus();
    }, BROWSER_OAUTH_POLL_MS);
  }

  async function toggleListener(): Promise<void> {
    if (!state.authState.listenerRunning) {
      await createListenerUrl();
      return;
    }
    stopBrowserOauthPolling();
    try {
      const status = await oauthStopRequest();
      applyOauthStatus(status);
    } catch {
      setState((previous) => ({
        ...previous,
        authState: {
          ...previous.authState,
          listenerRunning: false,
          initStatus: 'listener stopped',
        },
      }));
    }
    pushRuntimeEvent('callback listener stopped');
  }

  async function restartListener(): Promise<void> {
    stopBrowserOauthPolling();
    try {
      const response = await oauthRestartRequest();
      const callbackUrl = response.callbackUrl ?? buildListenerUrl(DEFAULT_OAUTH_CALLBACK_PORT, '/auth/callback');
      const parts = callbackParts(callbackUrl);
      setState((previous) => ({
        ...previous,
        browserAuthUrl: response.authorizationUrl ?? previous.browserAuthUrl,
        authState: {
          ...previous.authState,
          callbackPath: parts.callbackPath,
          listenerPort: parts.listenerPort,
          listenerUrl: parts.listenerUrl,
          listenerRunning: true,
          initStatus: 'pending',
          lastResponse: previous.authState.lastResponse,
        },
      }));
      pushRuntimeEvent('callback listener restarted', 'success');
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to restart callback listener';
      setState((previous) => ({
        ...previous,
        authState: {
          ...previous.authState,
          initStatus: 'error',
          lastResponse: message,
        },
      }));
      pushRuntimeEvent(message, 'warn');
    }
  }

  async function initAuth0(): Promise<void> {
    try {
      const status = await oauthStatusRequest();
      applyOauthStatus(status);
      pushRuntimeEvent(`oauth status: ${status.status}`);
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to fetch OAuth status';
      setState((previous) => ({
        ...previous,
        authState: {
          ...previous.authState,
          initStatus: 'error',
          lastResponse: message,
        },
      }));
      pushRuntimeEvent(message, 'warn');
    }
  }

  async function captureAuthResponse(): Promise<void> {
    const token = oauthStateToken(state.browserAuthUrl);
    if (!token) {
      pushRuntimeEvent('callback ignored: start listener first', 'warn');
      return;
    }

    const code = `code_${Math.random().toString(36).slice(2, 10)}`;
    try {
      await fetch(`/auth/callback?code=${encodeURIComponent(code)}&state=${encodeURIComponent(token)}`, { method: 'GET' });
      const status = await oauthStatusRequest();
      applyOauthStatus(status);
      setState((previous) => ({
        ...previous,
        authState: {
          ...previous.authState,
          lastResponse: `${nowStamp()} ${code}`,
        },
      }));
      pushRuntimeEvent(`callback received (${code})`);
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to capture callback';
      pushRuntimeEvent(message, 'warn');
    }
  }

  function setInspectorOpen(open: boolean): void {
    setState((previous) => ({ ...previous, inspectorOpen: open }));
  }

  function setScoringWeight(key: 'alpha' | 'beta' | 'gamma' | 'delta' | 'zeta' | 'eta', value: number): void {
    const nextValue = Math.max(0, Math.min(1, value));
    setState((previous) => ({
      ...previous,
      scoringModel: {
        ...previous.scoringModel,
        [key]: nextValue,
      },
    }));
    const keyMap: Record<typeof key, keyof DashboardSettingsUpdate> = {
      alpha: 'routingScoreAlpha',
      beta: 'routingScoreBeta',
      gamma: 'routingScoreGamma',
      delta: 'routingScoreDelta',
      zeta: 'routingScoreZeta',
      eta: 'routingScoreEta',
    };
    void persistDashboardSettingsPatch({ [keyMap[key]]: nextValue });
  }

  function setHeadroomWeight(key: 'wp' | 'ws', value: number): void {
    const nextValue = Math.max(0, Math.min(1, value));
    setState((previous) => ({
      ...previous,
      scoringModel: {
        ...previous.scoringModel,
        [key]: nextValue,
      },
    }));
    if (key === 'wp') {
      void persistDashboardSettingsPatch({ routingHeadroomWeightPrimary: nextValue });
      return;
    }
    void persistDashboardSettingsPatch({ routingHeadroomWeightSecondary: nextValue });
  }

  function setStrategyParam(modeId: string, key: string, value: number): void {
    setRoutingModes((previous) =>
      previous.map((candidate) => {
        if (candidate.id !== modeId || !candidate.params) return candidate;
        return {
          ...candidate,
          params: {
            ...candidate.params,
            [key]: value,
          },
        };
      }),
    );
    if (modeId === 'success_rate_weighted' && key === 'rho') {
      void persistDashboardSettingsPatch({ routingSuccessRateRho: value });
    }
  }

  function setTheme(theme: ThemeMode): void {
    setState((previous) => ({ ...previous, theme }));
    void persistDashboardSettingsPatch({ theme });
  }

  function setUpstreamStreamTransport(transport: UpstreamStreamTransport): void {
    void persistDashboardSettingsPatch({ upstreamStreamTransport: transport });
  }

  function setStickyThreadsEnabled(enabled: boolean): void {
    void persistDashboardSettingsPatch({ stickyThreadsEnabled: enabled });
  }

  function setPreferEarlierResetAccounts(enabled: boolean): void {
    void persistDashboardSettingsPatch({ preferEarlierResetAccounts: enabled });
  }

  function setOpenaiCacheAffinityMaxAgeSeconds(seconds: number): void {
    void persistDashboardSettingsPatch({ openaiCacheAffinityMaxAgeSeconds: Math.max(1, seconds) });
  }

  function setSyncClusterName(clusterName: string): void {
    const value = clusterName.trim() === '' ? 'default' : clusterName.trim();
    const patch = { syncClusterName: value };
    void persistDashboardSettingsPatch(patch);
    void reconfigureSyncCluster(patch);
  }

  function setSyncSiteId(siteId: number): void {
    const patch = { syncSiteId: Math.max(1, Math.trunc(siteId)) };
    void persistDashboardSettingsPatch(patch);
    void reconfigureSyncCluster(patch);
  }

  function setSyncPort(port: number): void {
    const bounded = Math.min(65535, Math.max(1, Math.trunc(port)));
    const patch = { syncPort: bounded };
    void persistDashboardSettingsPatch(patch);
    void reconfigureSyncCluster(patch);
  }

  function setSyncDiscoveryEnabled(enabled: boolean): void {
    const patch = { syncDiscoveryEnabled: enabled };
    void persistDashboardSettingsPatch(patch);
    void reconfigureSyncCluster(patch);
  }

  function setSyncIntervalSeconds(seconds: number): void {
    const bounded = Math.min(86400, Math.max(0, Math.trunc(seconds)));
    void persistDashboardSettingsPatch({ syncIntervalSeconds: bounded });
  }

  function setSyncConflictResolution(strategy: SyncConflictResolution): void {
    void persistDashboardSettingsPatch({ syncConflictResolution: strategy });
  }

  function setSyncJournalRetentionDays(days: number): void {
    const bounded = Math.min(3650, Math.max(1, Math.trunc(days)));
    void persistDashboardSettingsPatch({ syncJournalRetentionDays: bounded });
  }

  function setSyncTlsEnabled(enabled: boolean): void {
    void persistDashboardSettingsPatch({ syncTlsEnabled: enabled });
  }

  function setFirewallDraft(value: string): void {
    setFirewallDraftIpAddress(value);
  }

  async function addFirewallIpAddress(): Promise<void> {
    const api = window.tightrope;
    const candidate = firewallDraftIpAddress.trim();
    if (!candidate || !api?.addFirewallIp) {
      return;
    }
    try {
      await api.addFirewallIp(candidate);
      setFirewallDraftIpAddress('');
      await refreshFirewallIps();
      pushRuntimeEvent(`firewall allowlist added ${candidate}`, 'success');
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to add firewall IP';
      pushRuntimeEvent(message, 'warn');
    }
  }

  async function removeFirewallIpAddress(ipAddress: string): Promise<void> {
    const api = window.tightrope;
    if (!api?.removeFirewallIp) {
      return;
    }
    try {
      await api.removeFirewallIp(ipAddress);
      await refreshFirewallIps();
      pushRuntimeEvent(`firewall allowlist removed ${ipAddress}`, 'success');
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to remove firewall IP';
      pushRuntimeEvent(message, 'warn');
    }
  }

  function clusterConfigFromSettings(settings: DashboardSettings, manualPeers: string[] = []): {
    cluster_name: string;
    site_id: number;
    sync_port: number;
    discovery_enabled: boolean;
    manual_peers: string[];
  } {
    return {
      cluster_name: settings.syncClusterName,
      site_id: settings.syncSiteId,
      sync_port: settings.syncPort,
      discovery_enabled: settings.syncDiscoveryEnabled,
      manual_peers: manualPeers,
    };
  }

  async function reconfigureSyncCluster(patch: DashboardSettingsUpdate): Promise<void> {
    const api = window.tightrope;
    if (!api || !clusterStatus.enabled) {
      return;
    }
    const nextSettings = {
      ...dashboardSettingsRef.current,
      ...patch,
    };
    try {
      await api.clusterDisable();
      await api.clusterEnable(clusterConfigFromSettings(nextSettings));
      await refreshClusterState();
      pushRuntimeEvent('sync cluster reconfigured', 'success');
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to reconfigure sync cluster';
      pushRuntimeEvent(message, 'warn');
    }
  }

  async function toggleSyncEnabled(): Promise<void> {
    const api = window.tightrope;
    if (!api) {
      return;
    }

    try {
      if (clusterStatus.enabled) {
        await api.clusterDisable();
        await refreshClusterState();
        pushRuntimeEvent('sync cluster disabled', 'warn');
        return;
      }
      await api.clusterEnable(clusterConfigFromSettings(dashboardSettingsRef.current));
      await refreshClusterState();
      pushRuntimeEvent('sync cluster enabled', 'success');
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to toggle sync cluster';
      pushRuntimeEvent(message, 'warn');
    }
  }

  function setManualPeer(value: string): void {
    setManualPeerAddress(value);
  }

  async function addManualPeer(): Promise<void> {
    const api = window.tightrope;
    const peer = manualPeerAddress.trim();
    if (!peer || !api?.addPeer) {
      return;
    }
    try {
      await api.addPeer(peer);
      setManualPeerAddress('');
      await refreshClusterState();
      pushRuntimeEvent(`sync peer added ${peer}`, 'success');
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to add sync peer';
      pushRuntimeEvent(message, 'warn');
    }
  }

  async function removeSyncPeer(siteId: string): Promise<void> {
    const api = window.tightrope;
    if (!api?.removePeer) {
      return;
    }
    try {
      await api.removePeer(siteId);
      await refreshClusterState();
      pushRuntimeEvent(`sync peer removed ${siteId}`, 'success');
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to remove sync peer';
      pushRuntimeEvent(message, 'warn');
    }
  }

  async function triggerSyncNow(): Promise<void> {
    const api = window.tightrope;
    if (!api?.triggerSync) {
      return;
    }
    try {
      await api.triggerSync();
      await refreshClusterState();
      pushRuntimeEvent('sync triggered', 'success');
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to trigger sync';
      pushRuntimeEvent(message, 'warn');
    }
  }

  function openBackendDialog(): void {
    setState((previous) => ({ ...previous, backendDialogOpen: true }));
  }

  function closeBackendDialog(): void {
    setState((previous) => ({ ...previous, backendDialogOpen: false }));
  }

  function openAuthDialog(): void {
    setState((previous) => ({ ...previous, authDialogOpen: true }));
  }

  function closeAuthDialog(): void {
    setState((previous) => ({ ...previous, authDialogOpen: false }));
  }

  function openAddAccountDialog(): void {
    stopBrowserOauthPolling();
    selectedImportFileRef.current = null;
    captureOauthAccountBaseline();
    setState((previous) => resetAddAccountTransientState({ ...previous, addAccountOpen: true }));
  }

  function closeAddAccountDialog(): void {
    stopBrowserOauthPolling();
    clearDeviceFlowTimers();
    selectedImportFileRef.current = null;
    setState((previous) => resetAddAccountTransientState({ ...previous, addAccountOpen: false }));
  }

  function setAddAccountStep(step: AddAccountStep): void {
    if (step !== 'stepBrowser') {
      stopBrowserOauthPolling();
    }
    setState((previous) => ({ ...previous, addAccountStep: step }));
  }

  function selectImportFile(file: File): void {
    selectedImportFileRef.current = file;
    setState((previous) => ({ ...previous, selectedFileName: file.name }));
  }

  async function submitImport(): Promise<void> {
    const file = selectedImportFileRef.current;
    if (!file) return;
    try {
      const payload = extractImportAccountPayload(JSON.parse(await readFileText(file)));
      if (!payload) {
        throw new Error('Import file must include an account email');
      }
      const imported = await importAccountRequest(payload.email, payload.provider);
      await refreshAccountsFromNative();
      setState((previous) => ({
        ...previous,
        addAccountStep: 'stepSuccess',
        successEmail: imported.email,
        successPlan: imported.provider || 'openai',
        selectedAccountDetailId: imported.accountId,
        addAccountError: 'Something went wrong.',
      }));
      pushRuntimeEvent(`account imported: ${imported.email}`, 'success');
      void refreshUsageTelemetryAfterAccountAdd(imported.accountId, imported.email);
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Import failed';
      setState((previous) => ({
        ...previous,
        addAccountStep: 'stepError',
        addAccountError: message,
      }));
      pushRuntimeEvent(message, 'warn');
    }
  }

  async function simulateBrowserAuth(): Promise<void> {
    if (state.addAccountStep !== 'stepBrowser') {
      setState((previous) => ({ ...previous, addAccountStep: 'stepBrowser' }));
    }
    captureOauthAccountBaseline();
    const authorizationUrl = await createListenerUrl();
    if (authorizationUrl) {
      startBrowserOauthPolling();
      window.open(authorizationUrl, '_blank', 'noopener,noreferrer');
      return;
    }
    pushRuntimeEvent('Browser authorization URL is not ready yet', 'warn');
  }

  async function submitManualCallback(): Promise<void> {
    const callbackUrl = state.manualCallback.trim();
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
      setState((previous) => ({
        ...previous,
        addAccountStep: 'stepError',
        addAccountError: message,
      }));
      pushRuntimeEvent(message, 'warn');
    } catch (error) {
      const message = error instanceof Error ? error.message : 'OAuth callback failed';
      setState((previous) => ({
        ...previous,
        addAccountStep: 'stepError',
        addAccountError: message,
      }));
      pushRuntimeEvent(message, 'warn');
    }
  }

  function setManualCallback(value: string): void {
    setState((previous) => ({ ...previous, manualCallback: value }));
  }

  function flashCopyLabel(target: 'copyAuthLabel' | 'copyDeviceLabel'): void {
    setState((previous) => ({ ...previous, [target]: 'Copied' }));
    if (copyTimerRef.current) clearTimeout(copyTimerRef.current);
    copyTimerRef.current = setTimeout(() => {
      setState((previous) => ({ ...previous, [target]: 'Copy' }));
    }, 1200);
  }

  async function copyBrowserAuthUrl(): Promise<void> {
    const value = state.browserAuthUrl.trim();
    if (!isValidOAuthAuthorizationUrl(value)) {
      pushRuntimeEvent('No browser authorization URL to copy', 'warn');
      return;
    }
    try {
      await writeClipboardText(value);
      flashCopyLabel('copyAuthLabel');
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to copy browser authorization URL';
      pushRuntimeEvent(message, 'warn');
    }
  }

  async function copyDeviceVerificationUrl(): Promise<void> {
    const value = state.deviceVerifyUrl.trim();
    if (!value) {
      pushRuntimeEvent('No device verification URL to copy', 'warn');
      return;
    }
    try {
      await writeClipboardText(value);
      flashCopyLabel('copyDeviceLabel');
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to copy device verification URL';
      pushRuntimeEvent(message, 'warn');
    }
  }

  async function startDeviceFlow(): Promise<void> {
    clearDeviceFlowTimers();
    captureOauthAccountBaseline();
    try {
      const start = await oauthStartRequest('device');
      const expiresIn = start.expiresInSeconds ?? DEFAULT_DEVICE_EXPIRES_SECONDS;
      const intervalSeconds = start.intervalSeconds ?? 5;
      setState((previous) => ({
        ...previous,
        addAccountStep: 'stepDevice',
        deviceVerifyUrl: start.verificationUrl ?? previous.deviceVerifyUrl,
        deviceUserCode: start.userCode ?? previous.deviceUserCode,
        deviceCountdownSeconds: expiresIn,
      }));

      const completeBody = {
        deviceAuthId: start.deviceAuthId ?? undefined,
        userCode: start.userCode ?? undefined,
      };
      void oauthCompleteRequest(completeBody).catch(() => {
        // completion polling failures are surfaced by status polling
      });

      deviceTimerRef.current = setInterval(() => {
        setState((previous) => {
          const next = previous.deviceCountdownSeconds - 1;
          if (next <= 0) {
            clearDeviceFlowTimers();
            return {
              ...previous,
              addAccountStep: 'stepError',
              addAccountError: 'Device code expired. Please try again.',
              deviceCountdownSeconds: 0,
            };
          }
          return { ...previous, deviceCountdownSeconds: next };
        });
      }, 1000);

      oauthPollRef.current = setInterval(() => {
        void oauthStatusRequest()
          .then(async (status) => {
            if (status.status === 'success') {
              clearDeviceFlowTimers();
              await finalizeOauthAccountSuccess(
                status.callbackUrl ?? null,
                state.deviceUserCode || `device-${Date.now().toString(36)}`,
                'device oauth completed and account imported',
              );
            } else if (status.status === 'error') {
              clearDeviceFlowTimers();
              const message = status.errorMessage ?? 'Device OAuth failed';
              setState((previous) => ({
                ...previous,
                addAccountStep: 'stepError',
                addAccountError: message,
              }));
              pushRuntimeEvent(message, 'warn');
            }
          })
          .catch(() => {
            // keep polling until timeout
          });
      }, Math.max(1000, intervalSeconds * 1000));
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Unable to start device oauth';
      setState((previous) => ({
        ...previous,
        addAccountStep: 'stepError',
        addAccountError: message,
      }));
      pushRuntimeEvent(message, 'warn');
    }
  }

  function cancelDeviceFlow(): void {
    clearDeviceFlowTimers();
    setState((previous) => ({ ...previous, addAccountStep: 'stepMethod', deviceCountdownSeconds: DEFAULT_DEVICE_EXPIRES_SECONDS }));
  }

  function showAddAccountError(message: string): void {
    setState((previous) => ({
      ...previous,
      addAccountStep: 'stepError',
      addAccountError: message,
    }));
  }

  function setAccountSearchQuery(query: string): void {
    setState((previous) => ({ ...previous, accountSearchQuery: query }));
  }

  function setAccountStatusFilter(filter: '' | AccountState): void {
    setState((previous) => ({ ...previous, accountStatusFilter: filter }));
  }

  function selectAccountDetail(accountId: string): void {
    setState((previous) => ({ ...previous, selectedAccountDetailId: accountId }));
  }

  async function pauseSelectedAccount(): Promise<void> {
    if (!selectedAccountDetail) {
      return;
    }
    try {
      await pauseAccountRequest(selectedAccountDetail.id);
      await refreshAccountsFromNative();
      pushRuntimeEvent(`account paused: ${selectedAccountDetail.name}`, 'warn');
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to pause account';
      pushRuntimeEvent(message, 'warn');
    }
  }

  async function reactivateSelectedAccount(): Promise<void> {
    if (!selectedAccountDetail) {
      return;
    }
    try {
      await reactivateAccountRequest(selectedAccountDetail.id);
      await refreshAccountsFromNative();
      pushRuntimeEvent(`account resumed: ${selectedAccountDetail.name}`, 'success');
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to resume account';
      pushRuntimeEvent(message, 'warn');
    }
  }

  async function deleteSelectedAccount(): Promise<void> {
    if (!selectedAccountDetail) {
      return;
    }
    try {
      await deleteAccountRequest(selectedAccountDetail.id);
      await refreshAccountsFromNative();
      setState((previous) => ({
        ...previous,
        selectedAccountDetailId: null,
      }));
      pushRuntimeEvent(`account deleted: ${selectedAccountDetail.name}`, 'warn');
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to delete account';
      pushRuntimeEvent(message, 'warn');
    }
  }

  async function refreshSelectedAccountTelemetry(): Promise<void> {
    if (!selectedAccountDetail) {
      return;
    }
    if (refreshingAccountTelemetryId === selectedAccountDetail.id) {
      return;
    }

    setRefreshingAccountTelemetryId(selectedAccountDetail.id);
    try {
      await refreshAccountUsageTelemetryRequest(selectedAccountDetail.id);
      await refreshAccountsFromNative();
      pushRuntimeEvent(`usage telemetry refreshed: ${selectedAccountDetail.name}`, 'success');
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Failed to refresh usage telemetry';
      pushRuntimeEvent(message, 'warn');
    } finally {
      setRefreshingAccountTelemetryId((current) => (current === selectedAccountDetail.id ? null : current));
    }
  }

  function setSessionsKindFilter(kind: AppRuntimeState['sessionsKindFilter']): void {
    setState((previous) => ({
      ...previous,
      sessionsKindFilter: kind,
      sessionsOffset: 0,
    }));
  }

  function prevSessionsPage(): void {
    setState((previous) => ({
      ...previous,
      sessionsOffset: Math.max(0, previous.sessionsOffset - SESSIONS_LIMIT),
    }));
  }

  function nextSessionsPage(): void {
    setState((previous) => ({
      ...previous,
      sessionsOffset: previous.sessionsOffset + SESSIONS_LIMIT,
    }));
  }

  function purgeStaleSessions(): void {
    setState((previous) => ({
      ...previous,
      sessions: previous.sessions.filter((session) => !session.stale),
      sessionsOffset: 0,
    }));
  }

  function openDrawer(rowId: string): void {
    setState((previous) => ({ ...previous, drawerRowId: rowId }));
  }

  function closeDrawer(): void {
    setState((previous) => ({ ...previous, drawerRowId: null }));
  }

  const kpis = deriveKpis(visibleRows, accounts);
  const routerState = currentRouterState(state.runtimeState);
  const selectedRouteAccount = accounts.find((account) => account.id === selectedRoute.accountId) ?? fallbackRouteAccount(selectedRoute.accountId);
  const selectedMetric = metrics.get(selectedRouteAccount.id);
  const drawerRow = state.rows.find((row) => row.id === state.drawerRowId) ?? null;
  const sessionsStart = Math.min(state.sessionsOffset + 1, sessionsView.filtered.length);
  const sessionsEnd = Math.min(state.sessionsOffset + SESSIONS_LIMIT, sessionsView.filtered.length);

  return {
    state,
    accounts,
    routingModes,
    dashboardSettings,
    firewallMode,
    firewallEntries,
    firewallDraftIpAddress,
    clusterStatus,
    manualPeerAddress,
    metrics,
    visibleRows,
    ensuredRouteId,
    routedAccountId,
    activeRoutingMode,
    filteredAccounts,
    selectedAccountDetail,
    isRefreshingSelectedAccountTelemetry,
    selectedRoute,
    selectedRouteAccount,
    selectedMetric,
    routerState,
    sessionsView,
    drawerRow,
    sessionsPaginationLabel: sessionsView.filtered.length > 0 ? `${sessionsStart}–${sessionsEnd} of ${sessionsView.filtered.length}` : '0 results',
    canPrevSessions: state.sessionsOffset > 0,
    canNextSessions: state.sessionsOffset + SESSIONS_LIMIT < sessionsView.filtered.length,
    kpis,
    trafficClockMs,
    trafficActiveWindowMs: TRAFFIC_ACTIVE_WINDOW_MS,
    deterministicAccountDetailValues,
    stableSparklinePercents,
    formatNumber,
    modeLabel: modeLabel(state.routingMode, routingModes),
    setCurrentPage,
    setSearchQuery,
    setSelectedAccountId,
    setSelectedRoute,
    setRoutingMode,
    setRuntimeAction,
    toggleRoutePause,
    toggleAutoRestart,
    createListenerUrl,
    toggleListener,
    restartListener,
    initAuth0,
    captureAuthResponse,
    setInspectorOpen,
    setScoringWeight,
    setHeadroomWeight,
    setStrategyParam,
    setUpstreamStreamTransport,
    setStickyThreadsEnabled,
    setPreferEarlierResetAccounts,
    setOpenaiCacheAffinityMaxAgeSeconds,
    setFirewallDraft,
    addFirewallIpAddress,
    removeFirewallIpAddress,
    toggleSyncEnabled,
    setSyncSiteId,
    setSyncPort,
    setSyncDiscoveryEnabled,
    setSyncClusterName,
    setManualPeer,
    addManualPeer,
    removeSyncPeer,
    setSyncIntervalSeconds,
    setSyncConflictResolution,
    setSyncJournalRetentionDays,
    setSyncTlsEnabled,
    triggerSyncNow,
    setTheme,
    openBackendDialog,
    closeBackendDialog,
    openAuthDialog,
    closeAuthDialog,
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
    setAccountSearchQuery,
    setAccountStatusFilter,
    selectAccountDetail,
    refreshSelectedAccountTelemetry,
    pauseSelectedAccount,
    reactivateSelectedAccount,
    deleteSelectedAccount,
    setSessionsKindFilter,
    prevSessionsPage,
    nextSessionsPage,
    purgeStaleSessions,
    openDrawer,
    closeDrawer,
  };
}
