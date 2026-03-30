import type { Account, RouteMetrics, RouteRow } from '../../shared/types';
import { statusClass } from '../../state/logic';
import { hashToUnit } from '../../shared/math';

interface RequestDrawerProps {
  row: RouteRow | null;
  accounts: Account[];
  metrics: Map<string, RouteMetrics>;
  formatNumber: (value: number) => string;
  onClose: () => void;
}

export function RequestDrawer({ row, accounts, metrics, formatNumber, onClose }: RequestDrawerProps) {
  const open = row !== null;
  if (!row) {
    return (
      <>
        <div className="drawer-backdrop" />
        <aside className="log-drawer" />
      </>
    );
  }

  const account = accounts.find((candidate) => candidate.id === row.accountId) ?? accounts[0];
  const scored = accounts
    .map((candidate) => {
      const metric = metrics.get(candidate.id);
      return {
        name: candidate.name,
        score: metric?.score ?? Infinity,
        picked: candidate.id === row.accountId,
      };
    })
    .sort((a, b) => a.score - b.score);

  const hasRetry = row.status === 'warn' || hashToUnit(`${row.id}-retry`) > 0.7;
  const failAccount = accounts.find((candidate) => candidate.id !== row.accountId) ?? account;
  const queueWait = Math.floor(hashToUnit(`${row.id}-queue`) * 15);
  const total = row.latency + Math.floor(hashToUnit(`${row.id}-total`) * 15);

  return (
    <>
      <div className={`drawer-backdrop${open ? ' open' : ''}`} onClick={onClose} />
      <aside className={`log-drawer${open ? ' open' : ''}`}>
        <header className="section-header">
          <div>
            <p className="eyebrow">Request</p>
            <h2>{row.id}</h2>
          </div>
          <button className="dialog-close" type="button" aria-label="Close" onClick={onClose}>
            &times;
          </button>
        </header>
        <div className="drawer-body">
          <div className="drawer-section">
            <div className="drawer-section-header">Request</div>
            <dl className="drawer-kv">
              <dt>ID</dt>
              <dd>{row.id}</dd>
              <dt>Time</dt>
              <dd>{row.time}</dd>
              <dt>Protocol</dt>
              <dd>{row.protocol}</dd>
              <dt>Model</dt>
              <dd>{row.model}</dd>
              <dt>Account</dt>
              <dd>{account.name}</dd>
              <dt>Tokens</dt>
              <dd>{formatNumber(row.tokens)}</dd>
              <dt>Latency</dt>
              <dd>{row.latency} ms</dd>
              <dt>Status</dt>
              <dd>
                <span className={`status-badge ${statusClass(row.status)}`}>{row.status}</span>
              </dd>
              <dt>Session</dt>
              <dd>{row.sessionId}</dd>
            </dl>
          </div>

          <div className="drawer-section">
            <div className="drawer-section-header">Routing decision</div>
            <table className="scoring-table">
              <thead>
                <tr>
                  <th>Account</th>
                  <th>Score</th>
                </tr>
              </thead>
              <tbody>
                {scored.map((entry) => (
                  <tr key={entry.name} className={entry.picked ? 'picked' : ''}>
                    <td>
                      {entry.name}
                      {entry.picked ? ' ←' : ''}
                    </td>
                    <td style={{ fontFamily: "'SF Mono',ui-monospace,monospace", fontSize: '11px' }}>
                      {Number.isFinite(entry.score) ? entry.score.toFixed(4) : '∞'}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>

          {hasRetry && (
            <div className="drawer-section">
              <div className="drawer-section-header">Retry chain</div>
              <div className="retry-chain">
                <div className="retry-step">
                  <span className="retry-dot fail" />
                  <span>{failAccount.name}</span>
                  <span style={{ color: 'var(--text-tertiary)', fontSize: '11px', marginLeft: 'auto' }}>
                    429 rate_limited · {Math.floor(hashToUnit(`${row.id}-retryms`) * 200 + 50)} ms
                  </span>
                </div>
                <div className="retry-step">
                  <span className="retry-dot ok" />
                  <span>{account.name}</span>
                  <span style={{ color: 'var(--text-tertiary)', fontSize: '11px', marginLeft: 'auto' }}>200 ok · {row.latency} ms</span>
                </div>
              </div>
            </div>
          )}

          <div className="drawer-section">
            <div className="drawer-section-header">Timing</div>
            <dl className="drawer-kv">
              <dt>Queue wait</dt>
              <dd>{queueWait} ms</dd>
              <dt>Upstream TTFB</dt>
              <dd>{Math.floor(row.latency * 0.6)} ms</dd>
              <dt>Stream duration</dt>
              <dd>{row.latency} ms</dd>
              <dt>Total</dt>
              <dd>{total} ms</dd>
            </dl>
          </div>
        </div>
      </aside>
    </>
  );
}
