export type AccountPlan = 'enterprise' | 'plus' | 'free';
export type AccountHealth = 'healthy' | 'strained';
export type AccountState = 'active' | 'paused' | 'rate_limited' | 'deactivated' | 'quota_blocked';
export type RowStatus = 'ok' | 'warn' | 'error';
export type Protocol = 'SSE' | 'WS' | 'Compact' | 'Transcribe';
export type AppPage = 'router' | 'trace' | 'accounts' | 'sessions' | 'logs' | 'settings';
export type AddAccountStep = 'stepMethod' | 'stepImport' | 'stepBrowser' | 'stepDevice' | 'stepSuccess' | 'stepError';
export type RouterRuntimeState = 'running' | 'paused' | 'degraded' | 'stopped';
export type SessionKind = 'codex_session' | 'sticky_thread' | 'prompt_cache';
export type ThemeMode = 'auto' | 'dark' | 'light';
export type AccountUsageRefreshStatus = 'unknown' | 'success' | 'failed' | 'auth_required';
export type UpstreamStreamTransport = 'default' | 'auto' | 'http' | 'websocket';
export type SyncConflictResolution = 'lww' | 'site_priority' | 'field_merge';
export type FirewallMode = 'allow_all' | 'allowlist_active';
export type ClusterRole = 'leader' | 'follower' | 'candidate' | 'standalone';
export type ClusterPeerState = 'connected' | 'disconnected' | 'unreachable';
export type ClusterPeerSource = 'mdns' | 'manual';

export interface Account {
  id: string;
  name: string;
  pinned?: boolean;
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
  hasPrimaryQuota?: boolean;
  hasSecondaryQuota?: boolean;
  quotaPrimaryWindowSeconds?: number | null;
  quotaSecondaryWindowSeconds?: number | null;
  quotaPrimaryResetAtMs?: number | null;
  quotaSecondaryResetAtMs?: number | null;
  failovers: number;
  note: string;
  telemetryBacked: boolean;
  needsTokenRefresh?: boolean;
  usageRefreshStatus?: AccountUsageRefreshStatus;
  usageRefreshMessage?: string | null;
  usageRefreshUpdatedAtMs?: number | null;
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
  routingStrategy?: string | null;
  routingScore?: number | null;
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

export type SyncEvent =
  | { type: 'journal_entry'; ts: number; seq: number; table: string; op: string }
  | { type: 'peer_state_change'; ts: number; site_id: string; state: ClusterPeerState; address: string }
  | { type: 'role_change'; ts: number; role: Exclude<ClusterRole, 'standalone'>; term: number; leader_id: string | null }
  | { type: 'commit_advance'; ts: number; commit_index: number; last_applied: number }
  | { type: 'term_change'; ts: number; term: number }
  | { type: 'ingress_batch'; ts: number; site_id: string; accepted: boolean; bytes: number; apply_duration_ms: number; replication_latency_ms: number }
  | { type: 'lag_alert'; ts: number; active: boolean; lagging_peers: number; max_lag: number }
  | { type: 'account_traffic'; ts: number; account_id: string; up_bytes: number; down_bytes: number; last_up_at_ms: number; last_down_at_ms: number }
  | { type: 'runtime_signal'; ts: number; level: 'info' | 'success' | 'warn' | 'error'; code: string; message: string; account_id: string | null };

export interface AppRuntimeState {
  currentPage: AppPage;
  selectedAccountId: string;
  selectedRouteId: string;
  selectedAccountDetailId: string | null;
  currentRoutedAccountId: string | null;
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
  syncTopologyDialogOpen: boolean;
  drawerRowId: string | null;
  theme: ThemeMode;
}

export interface DashboardSettings {
  theme: ThemeMode;
  stickyThreadsEnabled: boolean;
  upstreamStreamTransport: UpstreamStreamTransport;
  preferEarlierResetAccounts: boolean;
  routingStrategy: string;
  strictLockPoolContinuations: boolean;
  lockedRoutingAccountIds: string[];
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
  routingPlanModelPricingUsdPerMillion: string;
  syncClusterName: string;
  syncSiteId: number;
  syncPort: number;
  syncDiscoveryEnabled: boolean;
  syncIntervalSeconds: number;
  syncConflictResolution: SyncConflictResolution;
  syncJournalRetentionDays: number;
  syncTlsEnabled: boolean;
  syncRequireHandshakeAuth: boolean;
  syncClusterSharedSecret: string;
  syncTlsVerifyPeer: boolean;
  syncTlsCaCertificatePath: string;
  syncTlsCertificateChainPath: string;
  syncTlsPrivateKeyPath: string;
  syncTlsPinnedPeerCertificateSha256: string;
  syncSchemaVersion: number;
  syncMinSupportedSchemaVersion: number;
  syncAllowSchemaDowngrade: boolean;
  syncPeerProbeEnabled: boolean;
  syncPeerProbeIntervalMs: number;
  syncPeerProbeTimeoutMs: number;
  syncPeerProbeMaxPerRefresh: number;
  syncPeerProbeFailClosed: boolean;
  syncPeerProbeFailClosedFailures: number;
}

export interface DashboardSettingsUpdate {
  theme?: ThemeMode;
  stickyThreadsEnabled?: boolean;
  upstreamStreamTransport?: UpstreamStreamTransport;
  preferEarlierResetAccounts?: boolean;
  routingStrategy?: string;
  strictLockPoolContinuations?: boolean;
  lockedRoutingAccountIds?: string[];
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
  routingPlanModelPricingUsdPerMillion?: string;
  syncClusterName?: string;
  syncSiteId?: number;
  syncPort?: number;
  syncDiscoveryEnabled?: boolean;
  syncIntervalSeconds?: number;
  syncConflictResolution?: SyncConflictResolution;
  syncJournalRetentionDays?: number;
  syncTlsEnabled?: boolean;
  syncRequireHandshakeAuth?: boolean;
  syncClusterSharedSecret?: string;
  syncTlsVerifyPeer?: boolean;
  syncTlsCaCertificatePath?: string;
  syncTlsCertificateChainPath?: string;
  syncTlsPrivateKeyPath?: string;
  syncTlsPinnedPeerCertificateSha256?: string;
  syncSchemaVersion?: number;
  syncMinSupportedSchemaVersion?: number;
  syncAllowSchemaDowngrade?: boolean;
  syncPeerProbeEnabled?: boolean;
  syncPeerProbeIntervalMs?: number;
  syncPeerProbeTimeoutMs?: number;
  syncPeerProbeMaxPerRefresh?: number;
  syncPeerProbeFailClosed?: boolean;
  syncPeerProbeFailClosedFailures?: number;
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
  routingPinned?: boolean | null;
  planType?: string | null;
  loadPercent?: number | null;
  inflight?: number | null;
  latencyMs?: number | null;
  errorEwma?: number | null;
  stickyHitPercent?: number | null;
  requests24h?: number | null;
  failovers24h?: number | null;
  totalCost24hUsd?: number | null;
  costNorm?: number | null;
  quotaPrimaryPercent?: number | null;
  quotaSecondaryPercent?: number | null;
  quotaPrimaryWindowSeconds?: number | null;
  quotaSecondaryWindowSeconds?: number | null;
  quotaPrimaryResetAtMs?: number | null;
  quotaSecondaryResetAtMs?: number | null;
}

export interface RuntimeAccountsListResponse {
  accounts: RuntimeAccount[];
  error?: {
    code?: string;
    message: string;
  };
}

export interface RuntimeStickySession {
  sessionKey: string;
  accountId: string;
  kind?: SessionKind | null;
  updatedAtMs: number;
  expiresAtMs: number;
}

export interface RuntimeStickySessionsResponse {
  generatedAtMs: number;
  sessions: RuntimeStickySession[];
}

export interface RuntimeStickySessionsPurgeResponse {
  generatedAtMs: number;
  purged: number;
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
  routingStrategy?: string | null;
  routingScore?: number | null;
  sticky?: boolean | null;
  latencyMs?: number | null;
  totalTokens?: number | null;
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

export type SqlImportAction = 'new' | 'update' | 'skip' | 'invalid';

export interface SqlImportPreviewRow {
  sourceRowId: string;
  dedupeKey: string;
  email: string | null;
  provider: string | null;
  planType: string | null;
  hasAccessToken: boolean;
  hasRefreshToken: boolean;
  hasIdToken: boolean;
  action: SqlImportAction;
  reason: string;
}

export interface SqlImportPreviewResponse {
  source: {
    path: string;
    fileName: string;
    sizeBytes: number;
    modifiedAtMs: number;
    schemaFingerprint: string;
    truncated?: boolean;
  };
  totals: {
    scanned: number;
    newCount: number;
    updateCount: number;
    skipCount: number;
    invalidCount: number;
  };
  requiresSourceEncryptionKey?: boolean;
  requiresSourceDatabasePassphrase?: boolean;
  rows: SqlImportPreviewRow[];
  warnings?: string[];
}

export interface SqlImportApplyResponse {
  totals: {
    scanned: number;
    inserted: number;
    updated: number;
    skipped: number;
    invalid: number;
    failed: number;
  };
  warnings: string[];
}

export interface SqlImportPreviewRequestPayload {
  sourcePath: string;
  sourceEncryptionKey?: string;
  sourceDatabasePassphrase?: string;
  importWithoutOverwrite?: boolean;
  rowLimit?: number;
}

export interface SqlImportApplyOverride {
  sourceRowId: string;
  action: SqlImportAction;
}

export interface SqlImportApplyRequestPayload {
  sourcePath: string;
  sourceEncryptionKey?: string;
  sourceDatabasePassphrase?: string;
  importWithoutOverwrite?: boolean;
  overrides?: SqlImportApplyOverride[];
  rowLimit?: number;
}

export interface ClusterPeerStatus {
  site_id: string;
  address: string;
  state: ClusterPeerState;
  role: Exclude<ClusterRole, 'standalone'>;
  match_index: number;
  replication_lag_entries: number;
  consecutive_heartbeat_failures: number;
  consecutive_probe_failures: number;
  ingress_accepted_batches: number;
  ingress_rejected_batches: number;
  ingress_accepted_wire_bytes: number;
  ingress_rejected_wire_bytes: number;
  ingress_rejected_batch_too_large: number;
  ingress_rejected_backpressure: number;
  ingress_rejected_inflight_wire_budget: number;
  ingress_rejected_handshake_auth: number;
  ingress_rejected_handshake_schema: number;
  ingress_rejected_invalid_wire_batch: number;
  ingress_rejected_entry_limit: number;
  ingress_rejected_rate_limit: number;
  ingress_rejected_apply_batch: number;
  ingress_rejected_ingress_protocol: number;
  ingress_last_wire_batch_bytes: number;
  ingress_total_apply_duration_ms: number;
  ingress_last_apply_duration_ms: number;
  ingress_max_apply_duration_ms: number;
  ingress_apply_duration_ewma_ms: number;
  ingress_apply_duration_samples: number;
  ingress_total_replication_latency_ms: number;
  ingress_last_replication_latency_ms: number;
  ingress_max_replication_latency_ms: number;
  ingress_replication_latency_ewma_ms: number;
  ingress_replication_latency_samples: number;
  ingress_inflight_wire_batches: number;
  ingress_inflight_wire_batches_peak: number;
  ingress_inflight_wire_bytes: number;
  ingress_inflight_wire_bytes_peak: number;
  last_heartbeat_at: number | null;
  last_probe_at: number | null;
  last_probe_duration_ms: number | null;
  last_probe_error: string | null;
  last_ingress_rejection_at: number | null;
  last_ingress_rejection_reason: string | null;
  last_ingress_rejection_error: string | null;
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
  replication_lagging_peers: number;
  replication_lag_total_entries: number;
  replication_lag_max_entries: number;
  replication_lag_avg_entries: number;
  replication_lag_ewma_entries: number;
  replication_lag_ewma_samples: number;
  replication_lag_alert_threshold_entries: number;
  replication_lag_alert_sustained_refreshes: number;
  replication_lag_alert_streak: number;
  replication_lag_alert_active: boolean;
  replication_lag_last_alert_at: number | null;
  ingress_socket_accept_failures: number;
  ingress_socket_accepted_connections: number;
  ingress_socket_completed_connections: number;
  ingress_socket_failed_connections: number;
  ingress_socket_active_connections: number;
  ingress_socket_peak_active_connections: number;
  ingress_socket_tls_handshake_failures: number;
  ingress_socket_read_failures: number;
  ingress_socket_apply_failures: number;
  ingress_socket_handshake_ack_failures: number;
  ingress_socket_bytes_read: number;
  ingress_socket_total_connection_duration_ms: number;
  ingress_socket_last_connection_duration_ms: number;
  ingress_socket_max_connection_duration_ms: number;
  ingress_socket_connection_duration_ewma_ms: number;
  ingress_socket_connection_duration_le_10ms: number;
  ingress_socket_connection_duration_le_50ms: number;
  ingress_socket_connection_duration_le_250ms: number;
  ingress_socket_connection_duration_le_1000ms: number;
  ingress_socket_connection_duration_gt_1000ms: number;
  ingress_socket_max_buffered_bytes: number;
  ingress_socket_max_queued_frames: number;
  ingress_socket_max_queued_payload_bytes: number;
  ingress_socket_paused_read_cycles: number;
  ingress_socket_paused_read_sleep_ms: number;
  ingress_socket_last_connection_at: number | null;
  ingress_socket_last_failure_at: number | null;
  ingress_socket_last_failure_error: string | null;
  journal_entries: number;
  pending_raft_entries: number;
  last_sync_at: number | null;
}

export interface ElectronApi {
  platform: NodeJS.Platform;
  getAppMeta: () => Promise<AppMetaResponse>;
  getHealth: () => Promise<{ status: 'ok' | 'degraded' | 'error'; uptime_ms: number }>;
  getSettings: () => Promise<DashboardSettings>;
  updateSettings: (update: DashboardSettingsUpdate) => Promise<DashboardSettings>;
  changeDatabasePassphrase: (payload: { currentPassphrase: string; nextPassphrase: string }) => Promise<{ status: string }>;
  exportDatabase: () => Promise<{ success: boolean; error?: string }>;
  oauthStart: (payload?: { forceMethod?: string }) => Promise<OauthStartResponse>;
  oauthStatus: () => Promise<OauthStatusResponse>;
  oauthStop: () => Promise<OauthStatusResponse>;
  oauthRestart: () => Promise<OauthStartResponse>;
  oauthComplete: (payload: { deviceAuthId?: string; userCode?: string }) => Promise<OauthCompleteResponse>;
  oauthManualCallback: (callbackUrl: string) => Promise<ManualCallbackResponse>;
  onOauthDeepLink: (listener: (event: OauthDeepLinkEvent) => void) => () => void;
  onAboutOpen: (listener: () => void) => () => void;
  listAccounts: () => Promise<RuntimeAccountsListResponse>;
  listStickySessions: (payload?: { limit?: number; offset?: number }) => Promise<RuntimeStickySessionsResponse>;
  purgeStaleSessions: () => Promise<RuntimeStickySessionsPurgeResponse>;
  listRequestLogs: (payload?: { limit?: number; offset?: number }) => Promise<RuntimeRequestLogsResponse>;
  listAccountTraffic: () => Promise<RuntimeAccountTrafficResponse>;
  importAccount: (payload: { email: string; provider: string; access_token?: string; refresh_token?: string }) => Promise<RuntimeAccount>;
  previewSqlImport: (payload: SqlImportPreviewRequestPayload) => Promise<SqlImportPreviewResponse>;
  pickSqlImportSourcePath?: () => Promise<string | null>;
  applySqlImport: (payload: SqlImportApplyRequestPayload) => Promise<SqlImportApplyResponse>;
  pinAccount: (accountId: string) => Promise<{ status: string }>;
  unpinAccount: (accountId: string) => Promise<{ status: string }>;
  pauseAccount: (accountId: string) => Promise<{ status: string }>;
  reactivateAccount: (accountId: string) => Promise<{ status: string }>;
  deleteAccount: (accountId: string) => Promise<{ status: string }>;
  refreshAccountUsageTelemetry: (accountId: string) => Promise<RuntimeAccount>;
  refreshAccountToken: (accountId: string) => Promise<RuntimeAccount>;
  listFirewallIps: () => Promise<FirewallListResponse>;
  addFirewallIp: (ipAddress: string) => Promise<FirewallIpEntry>;
  removeFirewallIp: (ipAddress: string) => Promise<{ status: string }>;
  getBackendStatus: () => Promise<{ enabled: boolean }>;
  startBackend: () => Promise<{ enabled: boolean }>;
  stopBackend: () => Promise<{ enabled: boolean }>;
  clusterEnable: (config: unknown) => Promise<void>;
  clusterDisable: () => Promise<void>;
  getClusterStatus: () => Promise<ClusterStatus>;
  addPeer: (address: string) => Promise<void>;
  removePeer: (siteId: string) => Promise<void>;
  triggerSync: () => Promise<void>;
  rollbackSyncBatch: (batchId: string) => Promise<void>;
  onSyncEvent: (listener: (event: SyncEvent) => void) => () => void;
  windowMinimize: () => Promise<void>;
  windowToggleMaximize: () => Promise<void>;
  windowClose: () => Promise<void>;
  windowIsMaximized: () => Promise<boolean>;
}

export interface AppMetaResponse {
  version: string;
  buildChannel: string;
  packaged: boolean;
}
