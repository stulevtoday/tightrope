import type { AccountState, AppRuntimeState, AuthState, RoutingMode, RuntimeState, ScoringModel } from '../shared/types';

export const HARD_BLOCKED_STATES = new Set<AccountState>(['paused', 'deactivated', 'rate_limited', 'quota_blocked']);

export const DEFAULT_ROUTING_MODES: RoutingMode[] = [
  {
    id: 'round_robin',
    label: 'Round Robin',
    desc: 'Cycle through eligible accounts in fixed order. Ignores load, latency, and quota.',
    formula: 'pick = accounts[i % N], i = i + 1',
    usesComposite: false,
  },
  {
    id: 'weighted_round_robin',
    label: 'Weighted Round Robin',
    desc: 'Deficit-based round robin with usage-weighted fairness to spread load smoothly.',
    formula: 'credit(a) += w(a) each cycle, pick = argmax_a credit(a), then credit(a) -= 1',
    usesComposite: false,
  },
  {
    id: 'drain_hop',
    label: 'Drain Hop',
    desc: 'Stick to one routed account until it is exhausted, then hop to the next eligible account.',
    formula: 'pick = active_account until exhausted; then pick next eligible account',
    usesComposite: false,
  },
];

const DEFAULT_RUNTIME_STATE: RuntimeState = {
  backend: 'stopped',
  health: 'warn',
  pausedRoutes: false,
  autoRestart: true,
  bind: '127.0.0.1:2455',
  events: [],
};

const DEFAULT_AUTH_STATE: AuthState = {
  tenantDomain: 'auth.openai.com',
  clientId: 'app_EMoamEEZ73f0CkXaXp7hrann',
  audience: 'oauth',
  callbackPath: '/auth/callback',
  listenerPort: 1455,
  listenerUrl: 'http://localhost:1455/auth/callback',
  listenerRunning: false,
  initStatus: 'not started',
  lastResponse: 'none',
};

const DEFAULT_SCORING_MODEL: ScoringModel = {
  wp: 0.35,
  ws: 0.65,
  alpha: 0.3,
  beta: 0.25,
  gamma: 0.2,
  delta: 0.2,
  zeta: 0.05,
  eta: 1,
  eps: 1e-9,
};

export function createInitialRuntimeState(): AppRuntimeState {
  return {
    currentPage: 'router',
    selectedAccountId: '',
    selectedRouteId: '',
    selectedAccountDetailId: null,
    currentRoutedAccountId: null,
    searchQuery: '',
    routingMode: 'weighted_round_robin',
    roundRobinCursor: 0,
    inspectorOpen: false,
    runtimeState: { ...DEFAULT_RUNTIME_STATE },
    authState: { ...DEFAULT_AUTH_STATE },
    scoringModel: { ...DEFAULT_SCORING_MODEL },
    rows: [],
    sessions: [],
    sessionsOffset: 0,
    sessionsKindFilter: 'all',
    accountSearchQuery: '',
    accountStatusFilter: '',
    addAccountOpen: false,
    addAccountStep: 'stepMethod',
    selectedFileName: '',
    manualCallback: '',
    browserAuthUrl: '',
    deviceVerifyUrl: 'https://auth.openai.com/codex/device',
    deviceUserCode: '',
    deviceCountdownSeconds: 900,
    copyAuthLabel: 'Copy',
    copyDeviceLabel: 'Copy',
    successEmail: '',
    successPlan: '',
    addAccountError: '',
    backendDialogOpen: false,
    authDialogOpen: false,
    syncTopologyDialogOpen: false,
    drawerRowId: null,
    theme: 'auto',
  };
}
