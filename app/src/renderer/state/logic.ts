import { HARD_BLOCKED_STATES } from '../data/seed';
import type {
  Account,
  ClusterStatus,
  RouteMetrics,
  RouteRow,
  RouterRuntimeState,
  RoutingMode,
  RuntimeState,
  RowStatus,
  ScoringModel,
  StickySession,
} from '../shared/types';
import { hashToUnit, normalize } from '../shared/math';

export function formatNumber(value: number): string {
  return value.toLocaleString();
}

export function nowStamp(date = new Date()): string {
  return date.toTimeString().slice(0, 8);
}

export function statusClass(status: RowStatus): string {
  if (status === 'ok') return 'ok';
  if (status === 'warn') return 'warn';
  return 'error';
}

export function modeLabel(routingMode: string, modes: RoutingMode[]): string {
  return modes.find((mode) => mode.id === routingMode)?.label ?? routingMode;
}

export function currentRouterState(runtimeState: RuntimeState): RouterRuntimeState {
  if (runtimeState.backend !== 'running') return 'stopped';
  if (runtimeState.pausedRoutes) return 'paused';
  if (runtimeState.health !== 'ok') return 'degraded';
  return 'running';
}

export function shouldScheduleAutoSync(clusterStatus: ClusterStatus, syncIntervalSeconds: number): boolean {
  if (!clusterStatus.enabled || syncIntervalSeconds <= 0) {
    return false;
  }
  return clusterStatus.peers.some((peer) => peer.state === 'connected');
}

function compositeScore(
  ctx: { qNorm: number; lNorm: number; errorEwma: number; h: number; c: number; costNorm: number },
  scoringModel: ScoringModel,
): number {
  return (
    scoringModel.alpha * ctx.qNorm +
    scoringModel.beta * ctx.lNorm +
    scoringModel.gamma * ctx.errorEwma +
    scoringModel.delta * (1 - ctx.h) +
    scoringModel.zeta * ctx.costNorm +
    scoringModel.eta * ctx.c
  );
}

function computeModeScore(
  ctx: { index: number; qNorm: number; lNorm: number; errorEwma: number; h: number; c: number; costNorm: number },
  routingMode: string,
  roundRobinCursor: number,
  accountsCount: number,
  scoringModel: ScoringModel,
  routingModes: RoutingMode[],
): number {
  switch (routingMode) {
    case 'round_robin':
    case 'weighted_round_robin':
      return ((ctx.index - roundRobinCursor + accountsCount) % accountsCount) / (accountsCount + scoringModel.eps);
    case 'least_outstanding_requests':
      return ctx.qNorm;
    case 'latency_ewma':
      return ctx.lNorm;
    case 'usage_weighted':
    case 'headroom_score':
      return 1 - ctx.h;
    case 'success_rate_weighted': {
      const mode = routingModes.find((candidate) => candidate.id === 'success_rate_weighted');
      const rho = mode?.params?.rho ?? 2;
      const pSuccess = Math.max(scoringModel.eps, 1 - ctx.errorEwma);
      return 1 / (Math.pow(pSuccess, rho) + scoringModel.eps);
    }
    case 'cost_aware':
      return ctx.costNorm;
    case 'deadline_aware': {
      const slackFactor = 2.0;
      return (
        scoringModel.alpha * ctx.qNorm +
        scoringModel.beta * slackFactor * ctx.lNorm +
        scoringModel.gamma * slackFactor * ctx.errorEwma +
        scoringModel.delta * (1 - ctx.h) +
        scoringModel.zeta * ctx.costNorm +
        scoringModel.eta * ctx.c
      );
    }
    case 'power_of_two_choices':
    default:
      return compositeScore(ctx, scoringModel);
  }
}

export function deriveMetrics(
  accounts: Account[],
  scoringModel: ScoringModel,
  routingMode: string,
  roundRobinCursor: number,
  routingModes: RoutingMode[],
): Map<string, RouteMetrics> {
  const inflights = accounts.map((account) => account.inflight);
  const latencies = accounts.map((account) => account.latency);
  const costs = accounts.map((account) => account.costNorm);

  const minQ = Math.min(...inflights);
  const maxQ = Math.max(...inflights);
  const minL = Math.min(...latencies);
  const maxL = Math.max(...latencies);
  const minC = Math.min(...costs);
  const maxC = Math.max(...costs);

  const metrics = new Map<string, RouteMetrics>();
  accounts.forEach((account, index) => {
    const qNorm = normalize(account.inflight, minQ, maxQ, scoringModel.eps);
    const lNorm = normalize(account.latency, minL, maxL, scoringModel.eps);
    const costNorm = normalize(account.costNorm, minC, maxC, scoringModel.eps);
    const uPrimary = account.quotaPrimary / 100;
    const uSecondary = account.quotaSecondary / 100;
    const h = 1 - (scoringModel.wp * uPrimary + scoringModel.ws * uSecondary);
    const c = account.cooldown ? 1 : 0;
    const capability = account.capability && !HARD_BLOCKED_STATES.has(account.state);
    const score = capability
      ? computeModeScore(
          { index, qNorm, lNorm, costNorm, h, c, errorEwma: account.errorEwma },
          routingMode,
          roundRobinCursor,
          accounts.length,
          scoringModel,
          routingModes,
        )
      : Infinity;
    metrics.set(account.id, { qNorm, lNorm, costNorm, h, c, capability, score });
  });
  return metrics;
}

export function filteredRows(
  rows: RouteRow[],
  accounts: Account[],
  selectedAccountId: string,
  searchQuery: string,
): RouteRow[] {
  const query = searchQuery.trim().toLowerCase();
  return rows.filter((row) => {
    const accountName = accounts.find((account) => account.id === row.accountId)?.name ?? row.accountId;
    if (selectedAccountId && row.accountId !== selectedAccountId) {
      return false;
    }
    if (!query) return true;
    return [row.id, row.model, row.protocol, accountName, row.sessionId].join(' ').toLowerCase().includes(query);
  });
}

export function ensureSelectedRouteId(rows: RouteRow[], selectedRouteId: string): string {
  if (rows.length === 0) return selectedRouteId;
  if (rows.some((row) => row.id === selectedRouteId)) return selectedRouteId;
  return rows[0].id;
}

export function selectRoutedAccountId(accounts: Account[], metrics: Map<string, RouteMetrics>): string | null {
  let routedAccountId: string | null = null;
  let bestScore = Infinity;
  accounts.forEach((account) => {
    const metric = metrics.get(account.id);
    if (metric && metric.capability && Number.isFinite(metric.score) && metric.score < bestScore) {
      bestScore = metric.score;
      routedAccountId = account.id;
    }
  });
  return routedAccountId;
}

export function deriveKpis(rows: RouteRow[], accounts: Account[]): { rpm: number; p95: number; failover: number; sticky: number } {
  const latencies = rows.length > 0 ? rows.map((row) => row.latency).sort((a, b) => a - b) : [0];
  const p95Index = Math.max(0, Math.floor(latencies.length * 0.95) - 1);
  const p95 = latencies[p95Index] || 0;
  if (accounts.length === 0) {
    const rpm = Math.max(0, Math.round(1924 * (rows.length / 15 || 1)));
    return { rpm, p95, failover: 0, sticky: 0 };
  }
  const failover = accounts.reduce((sum, account) => sum + account.failovers, 0);
  const sticky = Math.round(accounts.reduce((sum, account) => sum + account.stickyHit, 0) / accounts.length);
  const rpm = Math.max(0, Math.round(1924 * (rows.length / 15 || 1)));
  return { rpm, p95, failover, sticky };
}

export function paginateSessions(
  sessions: StickySession[],
  kind: StickySession['kind'] | 'all',
  offset: number,
  limit: number,
): { filtered: StickySession[]; paged: StickySession[]; staleTotal: number } {
  const filtered = kind === 'all' ? sessions : sessions.filter((session) => session.kind === kind);
  const paged = filtered.slice(offset, offset + limit);
  const staleTotal = sessions.filter((session) => session.stale).length;
  return { filtered, paged, staleTotal };
}

export function buildListenerUrl(listenerPort: number, callbackPath: string): string {
  const normalizedPath = callbackPath.startsWith('/') ? callbackPath : `/${callbackPath}`;
  return `http://127.0.0.1:${listenerPort}${normalizedPath}`;
}

export function generateDeviceCode(seed = Date.now()): string {
  const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ';
  const part = (offset: number) =>
    Array.from({ length: 4 }, (_, idx) => {
      const value = Math.floor(hashToUnit(`${seed}-${offset}-${idx}`) * chars.length);
      return chars[value];
    }).join('');
  return `${part(0)}-${part(1)}`;
}

export function stableSparklinePercents(key: string, currentPct: number): number[] {
  return Array.from({ length: 7 }, (_, index) => {
    if (index === 6) return currentPct;
    const variance = 0.4 + hashToUnit(`${key}-${index}`) * 0.6;
    return Math.min(100, Math.max(5, currentPct * variance));
  });
}

export function deterministicAccountDetailValues(accountId: string): { tokenAge: number; hoursToReset: number; minutesToReset: number; daysToReset: number } {
  const tokenAge = Math.floor(hashToUnit(`${accountId}-token`) * 50) + 10;
  const hoursToReset = Math.floor(hashToUnit(`${accountId}-hours`) * 4) + 1;
  const minutesToReset = Math.floor(hashToUnit(`${accountId}-mins`) * 59);
  const daysToReset = Math.floor(hashToUnit(`${accountId}-days`) * 6) + 1;
  return { tokenAge, hoursToReset, minutesToReset, daysToReset };
}
