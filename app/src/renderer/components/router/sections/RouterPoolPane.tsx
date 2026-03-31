import type { Account, RouteMetrics } from '../../../shared/types';

interface RouterPoolPaneProps {
  accounts: Account[];
  metrics: Map<string, RouteMetrics>;
  routedAccountId: string | null;
  trafficNowMs: number;
  trafficActiveWindowMs: number;
  selectedAccountId: string;
  onSelectAccount: (accountId: string) => void;
  onOpenAddAccount: () => void;
}

export function RouterPoolPane({
  accounts,
  metrics,
  routedAccountId,
  trafficNowMs,
  trafficActiveWindowMs,
  selectedAccountId,
  onSelectAccount,
  onOpenAddAccount,
}: RouterPoolPaneProps) {
  return (
    <section className="pane object-pane">
      <header className="section-header">
        <div>
          <p className="eyebrow">Accounts</p>
          <h1>Routing pool</h1>
        </div>
        <button className="tool-btn" type="button" onClick={onOpenAddAccount}>
          + Add
        </button>
      </header>
      <div className="pane-body">
        <span style={{ display: 'none' }}>Focus: {accounts.find((account) => account.id === selectedAccountId)?.name ?? 'all accounts'}</span>
        <div className="accounts-list" id="accountsList">
          {accounts.map((account) => {
            const metric = metrics.get(account.id);
            const scoreText = metric && Number.isFinite(metric.score) ? metric.score.toFixed(3) : '∞';
            const isRouted = account.id === routedAccountId;
            const hasOutboundInFlight = (account.trafficUpBytes ?? 0) > (account.trafficDownBytes ?? 0);
            const upActive =
              hasOutboundInFlight ||
              ((account.trafficLastUpAtMs ?? 0) > 0 && trafficNowMs - (account.trafficLastUpAtMs ?? 0) <= trafficActiveWindowMs);
            const downActive =
              (account.trafficLastDownAtMs ?? 0) > 0 &&
              trafficNowMs - (account.trafficLastDownAtMs ?? 0) <= trafficActiveWindowMs;
            return (
              <button
                key={account.id}
                type="button"
                className={`account-item${account.id === selectedAccountId ? ' active' : ''}${isRouted ? ' routed' : ''}`}
                onClick={() => onSelectAccount(account.id)}
              >
                <div className="account-top">
                  <span className="account-name">{account.name}</span>
                  <div className="account-top-right">
                    <span className={`traffic-indicator${upActive || downActive ? ' active' : ''}`} aria-hidden="true">
                      <span className={`traffic-arrow up${upActive ? ' active' : ''}`}>↑</span>
                      <span className={`traffic-arrow down${downActive ? ' active' : ''}`}>↓</span>
                    </span>
                    {isRouted ? (
                      <span className="routed-badge">
                        <span className="routed-dot" />
                        routed
                      </span>
                    ) : (
                      <span className="account-plan">{account.plan}</span>
                    )}
                  </div>
                </div>
                <div className="account-meta-row">
                  <span className="account-meta">
                    Load <strong>{account.telemetryBacked ? `${account.load}%` : '—'}</strong>
                  </span>
                  <span className="account-meta">
                    Latency <strong>{account.telemetryBacked ? `${account.latency} ms` : '—'}</strong>
                  </span>
                </div>
                <div className="account-meta-row">
                  <span className="account-meta">
                    Score <strong>{scoreText}</strong>
                  </span>
                  <span className="account-meta">{metric?.capability ? 'eligible' : 'blocked'}</span>
                </div>
                <div className="mini-bar">
                  <div
                    className={`mini-fill${account.telemetryBacked && account.load >= 80 ? ' hot' : ''}`}
                    style={{ width: `${account.telemetryBacked ? account.load : 0}%` }}
                  />
                </div>
              </button>
            );
          })}
        </div>
      </div>
    </section>
  );
}
