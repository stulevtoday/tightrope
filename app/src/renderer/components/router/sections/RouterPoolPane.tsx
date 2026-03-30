import type { Account, RouteMetrics } from '../../../shared/types';

interface RouterPoolPaneProps {
  accounts: Account[];
  metrics: Map<string, RouteMetrics>;
  routedAccountId: string | null;
  selectedAccountId: string;
  onSelectAccount: (accountId: string) => void;
  onOpenAddAccount: () => void;
}

export function RouterPoolPane({
  accounts,
  metrics,
  routedAccountId,
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
            return (
              <button
                key={account.id}
                type="button"
                className={`account-item${account.id === selectedAccountId ? ' active' : ''}${isRouted ? ' routed' : ''}`}
                onClick={() => onSelectAccount(account.id)}
              >
                <div className="account-top">
                  <span className="account-name">{account.name}</span>
                  {isRouted ? (
                    <span className="routed-badge">
                      <span className="routed-dot" />
                      routed
                    </span>
                  ) : (
                    <span className="account-plan">{account.plan}</span>
                  )}
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
