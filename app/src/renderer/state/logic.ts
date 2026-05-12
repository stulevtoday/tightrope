import i18next from 'i18next';
import { HARD_BLOCKED_STATES } from './defaults';
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
  return value.toLocaleString(i18next.language);
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
    case 'drain_hop':
      return ((ctx.index - roundRobinCursor + accountsCount) % accountsCount) / (accountsCount + scoringModel.eps);
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
    return [row.id, row.model, row.protocol, accountName, row.sessionId, row.method ?? '', row.path ?? '', row.errorCode ?? '']
      .join(' ')
      .toLowerCase()
      .includes(query);
  });
}

export function ensureSelectedRouteId(rows: RouteRow[], selectedRouteId: string): string {
  if (rows.length === 0) return selectedRouteId;
  if (rows.some((row) => row.id === selectedRouteId)) return selectedRouteId;
  return rows[0].id;
}

function routeRowTimestampMs(row: RouteRow): number | null {
  if (typeof row.requestedAt !== 'string' || row.requestedAt.trim() === '') {
    return null;
  }
  const value = row.requestedAt.trim();
  const parsed = new Date(value.includes('T') ? value : value.replace(' ', 'T') + 'Z');
  const ms = parsed.getTime();
  return Number.isFinite(ms) ? ms : null;
}

export function selectRoutedAccountId(
  accounts: Account[],
  metrics: Map<string, RouteMetrics>,
  rows: RouteRow[] = [],
): string | null {
  const pinned = accounts.find((account) => account.pinned && account.state === 'active');
  if (pinned) {
    const metric = metrics.get(pinned.id);
    if (metric?.capability && Number.isFinite(metric.score)) {
      return pinned.id;
    }
  }

  let latestRoutedRowAccountId: string | null = null;
  let latestRoutedRowAt = Number.NEGATIVE_INFINITY;
  let latestRoutedRowIndex = -1;
  for (const [index, row] of rows.entries()) {
    if (!row.accountId) {
      continue;
    }
    const metric = metrics.get(row.accountId);
    if (!metric?.capability || !Number.isFinite(metric.score)) {
      continue;
    }

    const requestedAtMs = routeRowTimestampMs(row);
    const comparableTimestamp = requestedAtMs ?? Number.NEGATIVE_INFINITY;
    const moreRecent =
      comparableTimestamp > latestRoutedRowAt ||
      (comparableTimestamp === latestRoutedRowAt && index > latestRoutedRowIndex);
    if (moreRecent) {
      latestRoutedRowAt = comparableTimestamp;
      latestRoutedRowIndex = index;
      latestRoutedRowAccountId = row.accountId;
    }
  }
  if (latestRoutedRowAccountId !== null) {
    return latestRoutedRowAccountId;
  }

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
  const now = Date.now();
  const oneMinuteAgo = now - 60_000;
  const recentRows = rows.filter((row) => {
    const ts = routeRowTimestampMs(row);
    return ts !== null && ts >= oneMinuteAgo;
  });
  const rpm = recentRows.length;

  const observedLatencies = rows.map((row) => row.latency).filter((latency) => Number.isFinite(latency) && latency > 0);
  const fallbackLatencies = accounts
    .map((account) => account.latency)
    .filter((latency) => Number.isFinite(latency) && latency > 0);
  const latencySample = (observedLatencies.length > 0 ? observedLatencies : fallbackLatencies).sort((a, b) => a - b);
  const p95Index = latencySample.length > 0 ? Math.max(0, Math.ceil(latencySample.length * 0.95) - 1) : 0;
  const p95 = latencySample[p95Index] ?? 0;

  const telemetryFailover = accounts.reduce((sum, account) => sum + Math.max(0, account.failovers), 0);
  const telemetryStickyValues = accounts.map((account) => account.stickyHit).filter((value) => Number.isFinite(value) && value >= 0);
  const telemetrySticky =
    telemetryStickyValues.length > 0
      ? Math.round(telemetryStickyValues.reduce((sum, value) => sum + value, 0) / telemetryStickyValues.length)
      : 0;

  if (rows.length === 0) {
    return { rpm, p95, failover: telemetryFailover, sticky: telemetrySticky };
  }

  const rowsNewestFirst = rows.slice().sort((left, right) => {
    const leftTs = routeRowTimestampMs(left) ?? 0;
    const rightTs = routeRowTimestampMs(right) ?? 0;
    if (leftTs === rightTs) {
      return right.id.localeCompare(left.id);
    }
    return rightTs - leftTs;
  });
  const rowsChronological = rowsNewestFirst.reverse();

  let stickyEligible = 0;
  let stickyHits = 0;
  const lastAccountBySession = new Map<string, string>();

  let derivedFailovers = 0;
  const failoverWindowMs = 60_000;
  const lastErrorBySession = new Map<string, number>();

  for (const row of rowsChronological) {
    const rawSessionKey = row.sessionId?.trim() ?? '';
    const sessionKey = rawSessionKey.startsWith('/') ? '' : rawSessionKey;
    if (sessionKey !== '') {
      stickyEligible += 1;
      const previousAccount = lastAccountBySession.get(sessionKey);
      if (previousAccount && previousAccount === row.accountId) {
        stickyHits += 1;
      }
      lastAccountBySession.set(sessionKey, row.accountId);
    }

    const ts = routeRowTimestampMs(row);
    if (ts === null) {
      continue;
    }
    if (row.status === 'warn' || row.status === 'error' || (typeof row.statusCode === 'number' && row.statusCode >= 400)) {
      const failoverKey = sessionKey !== '' ? sessionKey : `${row.path ?? ''}#${row.model}`;
      if (failoverKey !== '') {
        lastErrorBySession.set(failoverKey, ts);
      }
      continue;
    }
    const failoverKey = sessionKey !== '' ? sessionKey : `${row.path ?? ''}#${row.model}`;
    if (failoverKey === '') {
      continue;
    }
    const lastErrorAt = lastErrorBySession.get(failoverKey);
    if (typeof lastErrorAt === 'number' && ts - lastErrorAt >= 0 && ts - lastErrorAt <= failoverWindowMs) {
      derivedFailovers += 1;
      lastErrorBySession.delete(failoverKey);
    }
  }

  const sticky = telemetrySticky > 0 ? telemetrySticky : stickyEligible > 0 ? Math.round((stickyHits / stickyEligible) * 100) : 0;
  const failover = telemetryFailover > 0 ? telemetryFailover : derivedFailovers;
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
