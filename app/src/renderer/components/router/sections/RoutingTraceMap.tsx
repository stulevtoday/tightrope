import { useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import type { Account, RouteMetrics, RouteRow } from '../../../shared/types';
import { statusClass } from '../../../state/logic';

type TraceStageStatus = 'idle' | 'ok' | 'warn' | 'error';

interface TraceStage {
  id: string;
  label: string;
  value: string;
  detail: string;
  status: TraceStageStatus;
}

interface TraceCandidate {
  account: Account;
  score: number | null;
  selected: boolean;
  status: TraceStageStatus;
  reason: string;
}

interface RoutingTraceMapProps {
  accounts: Account[];
  metrics: Map<string, RouteMetrics>;
  visibleRows: RouteRow[];
  selectedRoute: RouteRow;
  selectedRouteAccount: Account;
  lockedRoutingAccountIds: string[];
  strictLockPoolContinuations: boolean;
  formatNumber: (value: number) => string;
}

function requestLabel(row: RouteRow): string {
  if (row.path) {
    return `${row.method ?? 'POST'} ${row.path}`;
  }
  return row.id || '—';
}

function routeTraceStatus(row: RouteRow): TraceStageStatus {
  if (!row.id) {
    return 'idle';
  }
  if (row.status === 'error') {
    return 'error';
  }
  if (row.status === 'warn') {
    return 'warn';
  }
  return 'ok';
}

function guardStatus(row: RouteRow): TraceStageStatus {
  if (!row.id) {
    return 'idle';
  }
  if (row.statusCode === 401 || row.statusCode === 403) {
    return 'error';
  }
  if (typeof row.statusCode === 'number' && row.statusCode >= 400) {
    return 'warn';
  }
  return 'ok';
}

function upstreamStatus(row: RouteRow): TraceStageStatus {
  if (!row.id) {
    return 'idle';
  }
  if (typeof row.statusCode === 'number' && row.statusCode >= 500) {
    return 'error';
  }
  if (typeof row.statusCode === 'number' && row.statusCode >= 400) {
    return 'warn';
  }
  return routeTraceStatus(row);
}

function scoreText(score: number | null | undefined): string {
  return typeof score === 'number' && Number.isFinite(score) ? score.toFixed(3) : '∞';
}

function finiteScore(metric: RouteMetrics | undefined): number | null {
  if (!metric || !Number.isFinite(metric.score)) {
    return null;
  }
  return metric.score;
}

function candidateStatus(account: Account, metric: RouteMetrics | undefined, selected: boolean): TraceStageStatus {
  if (selected) {
    return 'ok';
  }
  if (!metric?.capability || account.state !== 'active') {
    return 'error';
  }
  if (account.cooldown || account.health === 'strained') {
    return 'warn';
  }
  return 'idle';
}

export function RoutingTraceMap({
  accounts,
  metrics,
  visibleRows,
  selectedRoute,
  selectedRouteAccount,
  lockedRoutingAccountIds,
  strictLockPoolContinuations,
  formatNumber,
}: RoutingTraceMapProps) {
  const { t } = useTranslation();
  const hasRoute = visibleRows.length > 0 && selectedRoute.id.trim() !== '';
  const selectedMetric = metrics.get(selectedRoute.accountId);
  const selectedScore = finiteScore(selectedMetric);
  const lockPool = useMemo(
    () => new Set(lockedRoutingAccountIds.map((value) => value.trim()).filter((value) => value.length > 0)),
    [lockedRoutingAccountIds],
  );
  const selectedInLockPool = lockPool.has(selectedRoute.accountId);
  const routeStatus = routeTraceStatus(selectedRoute);
  const statusCodeText =
    typeof selectedRoute.statusCode === 'number' && selectedRoute.statusCode > 0
      ? String(selectedRoute.statusCode)
      : t(`common.status_${selectedRoute.status}`);
  const tokenText = selectedRoute.tokens > 0 ? formatNumber(selectedRoute.tokens) : '—';

  const stages: TraceStage[] = hasRoute
    ? [
        {
          id: 'client',
          label: t('router.trace_stage_client'),
          value: selectedRoute.model,
          detail: requestLabel(selectedRoute),
          status: 'ok',
        },
        {
          id: 'ingress',
          label: t('router.trace_stage_ingress'),
          value: selectedRoute.protocol,
          detail: selectedRoute.time,
          status: 'ok',
        },
        {
          id: 'guard',
          label: t('router.trace_stage_guard'),
          value: statusCodeText,
          detail: selectedRoute.errorCode ?? t('router.trace_guard_clear'),
          status: guardStatus(selectedRoute),
        },
        {
          id: 'affinity',
          label: t('router.trace_stage_affinity'),
          value: selectedRoute.sticky ? t('router.trace_sticky_hit') : t('router.trace_sticky_new'),
          detail:
            selectedRoute.sessionId && selectedRoute.sessionId !== selectedRoute.path
              ? selectedRoute.sessionId
              : t('router.trace_session_not_captured'),
          status: selectedRoute.sticky ? 'ok' : 'warn',
        },
        {
          id: 'scoring',
          label: t('router.trace_stage_scoring'),
          value: scoreText(selectedScore),
          detail: selectedRoute.routingStrategy ?? t('router.trace_strategy_unknown'),
          status: selectedScore === null ? 'warn' : 'ok',
        },
        {
          id: 'account',
          label: t('router.trace_stage_account'),
          value: selectedRouteAccount.name,
          detail: selectedInLockPool ? t('router.trace_account_locked') : t(`common.state_${selectedRouteAccount.state}`),
          status: selectedMetric?.capability === false ? 'warn' : routeStatus,
        },
        {
          id: 'upstream',
          label: t('router.trace_stage_upstream'),
          value: selectedRoute.protocol,
          detail: selectedRoute.errorCode ?? statusCodeText,
          status: upstreamStatus(selectedRoute),
        },
        {
          id: 'response',
          label: t('router.trace_stage_response'),
          value: `${selectedRoute.latency} ${t('common.ms_unit')}`,
          detail: t('router.trace_response_tokens', { tokens: tokenText }),
          status: routeStatus,
        },
      ]
    : [];

  const candidates: TraceCandidate[] = useMemo(() => {
    const selectedAccountId = selectedRoute.accountId;
    const selectedScoreValue = selectedScore ?? Infinity;
    return accounts
      .map((account) => {
        const metric = metrics.get(account.id);
        const accountScore = finiteScore(metric);
        const selected = account.id === selectedAccountId;
        const lockedOut = lockPool.size > 0 && !lockPool.has(account.id);
        let reason = '';
        if (selected) {
          if (selectedRoute.sticky) {
            reason = t('router.trace_reason_sticky_reused');
          } else if (lockPool.has(account.id)) {
            reason = t('router.trace_reason_lock_pool');
          } else {
            reason = t('router.trace_reason_selected_score', { score: scoreText(accountScore) });
          }
        } else if (!metric?.capability || account.state !== 'active') {
          reason = t('router.trace_reason_blocked_state', { state: t(`common.state_${account.state}`) });
        } else if (lockedOut) {
          reason = strictLockPoolContinuations
            ? t('router.trace_reason_strict_lock_pool')
            : t('router.trace_reason_outside_lock_pool');
        } else if (account.cooldown) {
          reason = t('router.trace_reason_cooldown');
        } else if (accountScore !== null && Number.isFinite(selectedScoreValue)) {
          reason = t('router.trace_reason_higher_score', {
            delta: Math.max(0, accountScore - selectedScoreValue).toFixed(3),
          });
        } else {
          reason = t('router.trace_reason_standby');
        }
        return {
          account,
          score: accountScore,
          selected,
          status: candidateStatus(account, metric, selected),
          reason,
        };
      })
      .sort((left, right) => {
        if (left.selected !== right.selected) {
          return left.selected ? -1 : 1;
        }
        const leftScore = left.score ?? Infinity;
        const rightScore = right.score ?? Infinity;
        if (leftScore !== rightScore) {
          return leftScore - rightScore;
        }
        return left.account.name.localeCompare(right.account.name, undefined, { sensitivity: 'base' });
      })
      .slice(0, 5);
  }, [accounts, lockPool, metrics, selectedRoute.accountId, selectedRoute.sticky, selectedScore, strictLockPoolContinuations, t]);

  return (
    <section className={`trace-map trace-${routeStatus}`} aria-label={t('router.trace_title')}>
      <header className="trace-map-header">
        <div>
          <p className="eyebrow">{t('router.trace_eyebrow')}</p>
          <h3>{t('router.trace_title')}</h3>
        </div>
        {hasRoute ? (
          <div className="trace-map-meta">
            <span className="mono">{selectedRoute.id}</span>
            <span className={`status-badge ${statusClass(selectedRoute.status)}`}>{t(`common.status_${selectedRoute.status}`)}</span>
          </div>
        ) : null}
      </header>

      {!hasRoute ? (
        <div className="empty-state">{t('router.trace_empty')}</div>
      ) : (
        <div className="trace-map-body">
          <div className="trace-flow-wrap">
            <div className="trace-flow" aria-label={t('router.trace_flow_label')}>
              {stages.map((stage, index) => {
                const next = stages[index + 1];
                const linkStatus = next ? (stage.status === 'error' || next.status === 'error' ? 'error' : stage.status === 'warn' || next.status === 'warn' ? 'warn' : 'ok') : 'idle';
                return (
                  <div key={stage.id} className={`trace-step${index === stages.length - 1 ? ' last' : ''}`}>
                    <div className={`trace-node ${stage.status}`} title={`${stage.label}: ${stage.detail}`}>
                      <span className="trace-node-label">{stage.label}</span>
                      <strong>{stage.value}</strong>
                      <span className="trace-node-detail">{stage.detail}</span>
                    </div>
                    {next ? <div className={`trace-link ${linkStatus}`} aria-hidden="true" /> : null}
                  </div>
                );
              })}
            </div>
          </div>

          <aside className="trace-sidecar">
            <div className="trace-candidate-panel">
              <div className="trace-sidecar-heading">
                <strong>{t('router.trace_candidates_title')}</strong>
                <span>{t('router.trace_candidates_count', { count: accounts.length })}</span>
              </div>
              <div className="trace-candidate-list">
                {candidates.map((candidate) => (
                  <div
                    key={candidate.account.id}
                    className={`trace-candidate ${candidate.status}${candidate.selected ? ' selected' : ''}`}
                  >
                    <div className="trace-candidate-copy">
                      <strong>{candidate.account.name}</strong>
                      <span>{candidate.reason}</span>
                    </div>
                    <span className="trace-candidate-score">{scoreText(candidate.score)}</span>
                  </div>
                ))}
              </div>
            </div>

            <div className={`trace-diagnosis ${routeStatus}`}>
              <span>{t('router.trace_diagnosis_label')}</span>
              <strong>
                {selectedRoute.status === 'ok'
                  ? t('router.trace_diagnosis_ok')
                  : selectedRoute.status === 'warn'
                    ? t('router.trace_diagnosis_warn')
                    : t('router.trace_diagnosis_error')}
              </strong>
              <p>{selectedRoute.errorCode ?? t('router.trace_diagnosis_no_error')}</p>
            </div>
          </aside>
        </div>
      )}
    </section>
  );
}
