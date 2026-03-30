import type { Account } from '../../shared/types';

interface AccountDetailPanelProps {
  selectedAccountDetail: Account | null;
  stableSparklinePercents: (key: string, currentPct: number) => number[];
  deterministicAccountDetailValues: (accountId: string) => {
    tokenAge: number;
    hoursToReset: number;
    minutesToReset: number;
    daysToReset: number;
  };
  formatNumber: (value: number) => string;
  isRefreshingUsageTelemetry: boolean;
  onRefreshUsageTelemetry: () => void;
  onPauseAccount: () => void;
  onReactivateAccount: () => void;
  onDeleteAccount: () => void;
}

function sparkline(bars: number[]) {
  return bars.map((pct, index) => (
    <div key={index} className="spark-bar" style={{ height: `${Math.max(2, pct * 0.24)}px` }} />
  ));
}

export function AccountDetailPanel({
  selectedAccountDetail,
  stableSparklinePercents,
  deterministicAccountDetailValues,
  formatNumber,
  isRefreshingUsageTelemetry,
  onRefreshUsageTelemetry,
  onPauseAccount,
  onReactivateAccount,
  onDeleteAccount,
}: AccountDetailPanelProps) {
  const detail = selectedAccountDetail;
  const detailValues = detail ? deterministicAccountDetailValues(detail.id) : null;
  const hasTelemetry = detail?.telemetryBacked ?? false;
  const accessTokenStateClass = detail?.state === 'deactivated' ? 'token-expired' : 'token-ok';
  const accessTokenStateLabel = detail?.state === 'deactivated' ? 'reauth required' : 'managed by provider';

  return (
    <div className="accounts-detail" id="accountDetailPanel">
      <header className="section-header">
        <div>
          <p className="eyebrow">Detail</p>
          <h2>{detail?.name ?? 'Select an account'}</h2>
        </div>
        <span style={{ fontSize: '11px', color: 'var(--text-tertiary)', fontFamily: "'SF Mono',ui-monospace,monospace" }}>{detail?.id ?? ''}</span>
      </header>
      <div className="detail-content">
        {!detail || !detailValues ? (
          <div className="empty-detail">
            <strong>Select an account</strong>
            <span>Choose an account from the list to view details</span>
          </div>
        ) : (
          <>
            <div className="detail-card">
              <div className="detail-card-header">
                <h4>Usage</h4>
                <button
                  className="btn-secondary"
                  type="button"
                  onClick={onRefreshUsageTelemetry}
                  disabled={isRefreshingUsageTelemetry}
                >
                  {isRefreshingUsageTelemetry ? 'Refreshing...' : 'Refresh usage'}
                </button>
              </div>
              <div className="usage-bar-row">
                <div className="usage-label-row">
                  <span>5h remaining</span>
                  <span>{hasTelemetry ? `${100 - detail.quotaPrimary}%` : '—'}</span>
                </div>
                <div className="usage-bar">
                  <div className="usage-fill" style={{ width: `${hasTelemetry ? 100 - detail.quotaPrimary : 0}%` }} />
                </div>
                <span className="usage-reset">
                  {hasTelemetry ? `Resets in ${detailValues.hoursToReset}h ${detailValues.minutesToReset}m` : 'No DB usage telemetry yet'}
                </span>
              </div>
              <div className="usage-bar-row">
                <div className="usage-label-row">
                  <span>Weekly remaining</span>
                  <span>{hasTelemetry ? `${100 - detail.quotaSecondary}%` : '—'}</span>
                </div>
                <div className="usage-bar">
                  <div className="usage-fill" style={{ width: `${hasTelemetry ? 100 - detail.quotaSecondary : 0}%` }} />
                </div>
                <span className="usage-reset">{hasTelemetry ? `Resets in ${detailValues.daysToReset}d` : 'No DB usage telemetry yet'}</span>
              </div>
            </div>

            <div className="detail-card">
              <h4>7-day usage trend</h4>
              {hasTelemetry ? (
                <>
                  <div className="trend-row">
                    <span className="trend-label">5h</span>
                    <div className="sparkline">{sparkline(stableSparklinePercents(`${detail.id}-p`, detail.quotaPrimary))}</div>
                  </div>
                  <div className="trend-row">
                    <span className="trend-label">Weekly</span>
                    <div className="sparkline">{sparkline(stableSparklinePercents(`${detail.id}-s`, detail.quotaSecondary))}</div>
                  </div>
                </>
              ) : (
                <div className="empty-detail">
                  <span>No historical usage telemetry in DB yet</span>
                </div>
              )}
            </div>

            <div className="detail-card">
              <h4>Request usage</h4>
              <div className="request-stats">
                <div className="request-stat">
                  <span>Requests</span>
                  {hasTelemetry ? formatNumber(detail.routed24h) : '—'}
                </div>
                <div className="request-stat">
                  <span>Tokens</span>
                  {hasTelemetry ? formatNumber(detail.routed24h * 1240) : '—'}
                </div>
                <div className="request-stat">
                  <span>Cost</span>{hasTelemetry ? `$${(detail.routed24h * 0.0032).toFixed(2)}` : '—'}
                </div>
                <div className="request-stat">
                  <span>Failovers</span>
                  {hasTelemetry ? detail.failovers : '—'}
                </div>
              </div>
            </div>

            <div className="detail-card">
              <h4>Token status</h4>
              <dl className="token-grid">
                <dt>Access</dt>
                <dd className={accessTokenStateClass}>{accessTokenStateLabel}</dd>
                <dt>Refresh</dt>
                <dd className="token-ok">managed by provider</dd>
                <dt>ID token</dt>
                <dd className="token-ok">managed by provider</dd>
              </dl>
            </div>

            <div className="account-actions">
              {detail.state === 'paused' ? (
                <button className="dock-btn accent" type="button" onClick={onReactivateAccount}>
                  Resume
                </button>
              ) : (
                <button className="dock-btn" type="button" onClick={onPauseAccount}>
                  Pause
                </button>
              )}
              {detail.state === 'deactivated' && (
                <button className="dock-btn" type="button">
                  Re-authenticate
                </button>
              )}
              <button className="btn-danger" type="button" onClick={onDeleteAccount}>
                Delete
              </button>
            </div>
          </>
        )}
      </div>
    </div>
  );
}
