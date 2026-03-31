export type AccountPlan = 'enterprise' | 'plus' | 'free';
export type AccountHealth = 'healthy' | 'strained';
export type AccountState = 'active' | 'paused' | 'rate_limited' | 'deactivated' | 'quota_blocked';
export type RowStatus = 'ok' | 'warn' | 'error';
export type Protocol = 'SSE' | 'WS' | 'Compact' | 'Transcribe';
export type AppPage = 'router' | 'accounts' | 'sessions' | 'logs' | 'settings';
export type AddAccountStep = 'stepMethod' | 'stepImport' | 'stepBrowser' | 'stepDevice' | 'stepSuccess' | 'stepError';
export type RouterRuntimeState = 'running' | 'paused' | 'degraded' | 'stopped';
export type SessionKind = 'codex_session' | 'sticky_thread' | 'prompt_cache';
export type ThemeMode = 'auto' | 'dark' | 'light';
export type UpstreamStreamTransport = 'default' | 'auto' | 'http' | 'websocket';
export type SyncConflictResolution = 'lww' | 'site_priority' | 'field_merge';
export type FirewallMode = 'allow_all' | 'allowlist_active';
export type ClusterRole = 'leader' | 'follower' | 'candidate' | 'standalone';
export type ClusterPeerState = 'connected' | 'disconnected' | 'unreachable';
export type ClusterPeerSource = 'mdns' | 'manual';

export interface Account {
  id: string;
  name: string;
  plan: AccountPlan;
  health: AccountHealth;
  state: AccountState;
  inflight: number;
  load: number;
  latency: number;
  errorEwma: number;
  cooldown: boolean;
  capability: boolean;
  costNorm: number;
  routed24h: number;
  stickyHit: number;
  quotaPrimary: number;
  quotaSecondary: number;
  failovers: number;
  note: string;
  telemetryBacked: boolean;
  trafficUpBytes?: number;
  trafficDownBytes?: number;
  trafficLastUpAtMs?: number;
  trafficLastDownAtMs?: number;
}

export interface RouteRow {
  time: string;
  id: string;
  model: string;
  accountId: string;
  tokens: number;
  latency: number;
  status: RowStatus;
  protocol: Protocol;
  sessionId: string;
  sticky: boolean;
  method?: string;
  path?: string;
  statusCode?: number;
  errorCode?: string | null;
  requestedAt?: string;
  totalCost?: number;
}

export interface RoutingMode {
  id: string;
  label: string;
  desc: string;
  formula: string;
  usesComposite: boolean;
  params?: Record<string, number>;
}

export interface ScoringModel {
  wp: number;
  ws: number;
  alpha: number;
  beta: number;
  gamma: number;
  delta: number;
  zeta: number;
  eta: number;
  eps: number;
}

export interface RuntimeState {
  backend: 'running' | 'stopped';
  health: 'ok' | 'warn';
  pausedRoutes: boolean;
  autoRestart: boolean;
  bind: string;
  events: string[];
}

export interface AuthState {
  tenantDomain: string;
  clientId: string;
  audience: string;
  callbackPath: string;
  listenerPort: number;
  listenerUrl: string;
  listenerRunning: boolean;
  initStatus: string;
  lastResponse: string;
}

export interface StickySession {
  key: string;
  kind: SessionKind;
  accountId: string;
  updated: string;
  expiry: string | null;
  stale: boolean;
}

export interface RouteMetrics {
  qNorm: number;
  lNorm: number;
  costNorm: number;
  h: number;
  c: number;
  capability: boolean;
  score: number;
}

export interface AppRuntimeState {
  currentPage: AppPage;
  selectedAccountId: string;
  selectedRouteId: string;
  selectedAccountDetailId: string | null;
  searchQuery: string;
  routingMode: string;
  roundRobinCursor: number;
  inspectorOpen: boolean;
  runtimeState: RuntimeState;
  authState: AuthState;
  scoringModel: ScoringModel;
  rows: RouteRow[];
  sessions: StickySession[];
  sessionsOffset: number;
  sessionsKindFilter: SessionKind | 'all';
  accountSearchQuery: string;
  accountStatusFilter: '' | AccountState;
  addAccountOpen: boolean;
  addAccountStep: AddAccountStep;
  selectedFileName: string;
  manualCallback: string;
  browserAuthUrl: string;
  deviceVerifyUrl: string;
  deviceUserCode: string;
  deviceCountdownSeconds: number;
  copyAuthLabel: 'Copy' | 'Copied';
  copyDeviceLabel: 'Copy' | 'Copied';
  successEmail: string;
  successPlan: string;
  addAccountError: string;
  backendDialogOpen: boolean;
  authDialogOpen: boolean;
  drawerRowId: string | null;
  theme: ThemeMode;
}

export interface DashboardSettings {
  theme: ThemeMode;
  stickyThreadsEnabled: boolean;
  upstreamStreamTransport: UpstreamStreamTransport;
  preferEarlierResetAccounts: boolean;
  routingStrategy: string;
  openaiCacheAffinityMaxAgeSeconds: number;
  importWithoutOverwrite: boolean;
  totpRequiredOnLogin: boolean;
  totpConfigured: boolean;
  apiKeyAuthEnabled: boolean;
  routingHeadroomWeightPrimary: number;
  routingHeadroomWeightSecondary: number;
  routingScoreAlpha: number;
  routingScoreBeta: number;
  routingScoreGamma: number;
  routingScoreDelta: number;
  routingScoreZeta: number;
  routingScoreEta: number;
  routingSuccessRateRho: number;
  syncClusterName: string;
  syncSiteId: number;
  syncPort: number;
  syncDiscoveryEnabled: boolean;
  syncIntervalSeconds: number;
  syncConflictResolution: SyncConflictResolution;
  syncJournalRetentionDays: number;
  syncTlsEnabled: boolean;
}

export interface DashboardSettingsUpdate {
  theme?: ThemeMode;
  stickyThreadsEnabled?: boolean;
  upstreamStreamTransport?: UpstreamStreamTransport;
  preferEarlierResetAccounts?: boolean;
  routingStrategy?: string;
  openaiCacheAffinityMaxAgeSeconds?: number;
  importWithoutOverwrite?: boolean;
  totpRequiredOnLogin?: boolean;
  apiKeyAuthEnabled?: boolean;
  routingHeadroomWeightPrimary?: number;
  routingHeadroomWeightSecondary?: number;
  routingScoreAlpha?: number;
  routingScoreBeta?: number;
  routingScoreGamma?: number;
  routingScoreDelta?: number;
  routingScoreZeta?: number;
  routingScoreEta?: number;
  routingSuccessRateRho?: number;
  syncClusterName?: string;
  syncSiteId?: number;
  syncPort?: number;
  syncDiscoveryEnabled?: boolean;
  syncIntervalSeconds?: number;
  syncConflictResolution?: SyncConflictResolution;
  syncJournalRetentionDays?: number;
  syncTlsEnabled?: boolean;
}

export interface FirewallIpEntry {
  ipAddress: string;
  createdAt: string;
}

export interface FirewallListResponse {
  mode: FirewallMode;
  entries: FirewallIpEntry[];
}

export interface OauthStartResponse {
  method: string;
  authorizationUrl: string | null;
  callbackUrl: string | null;
  verificationUrl: string | null;
  userCode: string | null;
  deviceAuthId: string | null;
  intervalSeconds: number | null;
  expiresInSeconds: number | null;
}

export interface OauthStatusResponse {
  status: string;
  errorMessage: string | null;
  listenerRunning?: boolean;
  callbackUrl?: string | null;
  authorizationUrl?: string | null;
}

export interface OauthCompleteResponse {
  status: string;
}

export interface ManualCallbackResponse {
  status: string;
  errorMessage: string | null;
}

export interface OauthDeepLinkEvent {
  kind: 'success' | 'callback';
  url: string;
}

export interface RuntimeAccount {
  accountId: string;
  email: string;
  provider: string;
  status: string;
  planType?: string | null;
  loadPercent?: number | null;
  inflight?: number | null;
  latencyMs?: number | null;
  errorEwma?: number | null;
  stickyHitPercent?: number | null;
  requests24h?: number | null;
  failovers24h?: number | null;
  costNorm?: number | null;
  quotaPrimaryPercent?: number | null;
  quotaSecondaryPercent?: number | null;
}

export interface RuntimeAccountsListResponse {
  accounts: RuntimeAccount[];
}

export interface RuntimeRequestLog {
  id: number;
  accountId: string | null;
  path: string;
  method: string;
  statusCode: number;
  model: string | null;
  requestedAt: string;
  errorCode: string | null;
  transport: string | null;
  totalCost: number;
}

export interface RuntimeRequestLogsResponse {
  limit: number;
  offset: number;
  logs: RuntimeRequestLog[];
}

export interface RuntimeAccountTraffic {
  accountId: string;
  upBytes: number;
  downBytes: number;
  lastUpAtMs: number;
  lastDownAtMs: number;
}

export interface RuntimeAccountTrafficResponse {
  generatedAtMs: number;
  accounts: RuntimeAccountTraffic[];
}

export interface ClusterPeerStatus {
  site_id: string;
  address: string;
  state: ClusterPeerState;
  role: Exclude<ClusterRole, 'standalone'>;
  match_index: number;
  last_heartbeat_at: number | null;
  discovered_via: ClusterPeerSource;
}

export interface ClusterStatus {
  enabled: boolean;
  site_id: string;
  cluster_name: string;
  role: ClusterRole;
  term: number;
  commit_index: number;
  leader_id: string | null;
  peers: ClusterPeerStatus[];
  journal_entries: number;
  pending_raft_entries: number;
  last_sync_at: number | null;
}

export interface ElectronApi {
  platform: NodeJS.Platform;
  getHealth: () => Promise<{ status: 'ok' | 'degraded' | 'error'; uptime_ms: number }>;
  getSettings: () => Promise<DashboardSettings>;
  updateSettings: (update: DashboardSettingsUpdate) => Promise<DashboardSettings>;
  oauthStart: (payload?: { forceMethod?: string }) => Promise<OauthStartResponse>;
  oauthStatus: () => Promise<OauthStatusResponse>;
  oauthStop: () => Promise<OauthStatusResponse>;
  oauthRestart: () => Promise<OauthStartResponse>;
  oauthComplete: (payload: { deviceAuthId?: string; userCode?: string }) => Promise<OauthCompleteResponse>;
  oauthManualCallback: (callbackUrl: string) => Promise<ManualCallbackResponse>;
  onOauthDeepLink: (listener: (event: OauthDeepLinkEvent) => void) => () => void;
  listAccounts: () => Promise<RuntimeAccountsListResponse>;
  listRequestLogs: (payload?: { limit?: number; offset?: number }) => Promise<RuntimeRequestLogsResponse>;
  listAccountTraffic: () => Promise<RuntimeAccountTrafficResponse>;
  importAccount: (payload: { email: string; provider: string }) => Promise<RuntimeAccount>;
  pauseAccount: (accountId: string) => Promise<{ status: string }>;
  reactivateAccount: (accountId: string) => Promise<{ status: string }>;
  deleteAccount: (accountId: string) => Promise<{ status: string }>;
  refreshAccountUsageTelemetry: (accountId: string) => Promise<RuntimeAccount>;
  listFirewallIps: () => Promise<FirewallListResponse>;
  addFirewallIp: (ipAddress: string) => Promise<FirewallIpEntry>;
  removeFirewallIp: (ipAddress: string) => Promise<{ status: string }>;
  clusterEnable: (config: unknown) => Promise<void>;
  clusterDisable: () => Promise<void>;
  getClusterStatus: () => Promise<ClusterStatus>;
  addPeer: (address: string) => Promise<void>;
  removePeer: (siteId: string) => Promise<void>;
  triggerSync: () => Promise<void>;
  rollbackSyncBatch: (batchId: string) => Promise<void>;
  windowMinimize: () => Promise<void>;
  windowToggleMaximize: () => Promise<void>;
  windowClose: () => Promise<void>;
  windowIsMaximized: () => Promise<boolean>;
}
