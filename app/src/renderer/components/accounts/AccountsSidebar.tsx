import type { Account } from '../../shared/types';

interface AccountsSidebarProps {
  filteredAccounts: Account[];
  selectedAccountDetail: Account | null;
  trafficNowMs: number;
  trafficActiveWindowMs: number;
  accountSearchQuery: string;
  accountStatusFilter: '' | 'active' | 'paused' | 'rate_limited' | 'deactivated' | 'quota_blocked';
  onOpenAddAccount: () => void;
  onSearch: (query: string) => void;
  onFilterStatus: (status: '' | 'active' | 'paused' | 'rate_limited' | 'deactivated' | 'quota_blocked') => void;
  onSelectDetail: (accountId: string) => void;
}

export function AccountsSidebar({
  filteredAccounts,
  selectedAccountDetail,
  trafficNowMs,
  trafficActiveWindowMs,
  accountSearchQuery,
  accountStatusFilter,
  onOpenAddAccount,
  onSearch,
  onFilterStatus,
  onSelectDetail,
}: AccountsSidebarProps) {
  return (
    <div className="accounts-sidebar">
      <header className="section-header">
        <div>
          <p className="eyebrow">Imported</p>
          <h2>Accounts</h2>
        </div>
        <button className="tool-btn" type="button" onClick={onOpenAddAccount}>
          + Add
        </button>
      </header>
      <div className="accounts-filter-bar">
        <input
          className="search"
          type="search"
          placeholder="Search accounts..."
          aria-label="Search accounts"
          value={accountSearchQuery}
          onChange={(event) => onSearch(event.target.value)}
        />
        <select value={accountStatusFilter} aria-label="Filter by status" onChange={(event) => onFilterStatus(event.target.value as AccountsSidebarProps['accountStatusFilter'])}>
          <option value="">All statuses</option>
          <option value="active">Active</option>
          <option value="paused">Paused</option>
          <option value="rate_limited">Rate limited</option>
          <option value="deactivated">Deactivated</option>
        </select>
      </div>
      <div className="pane-body">
        <div className="accounts-list">
          {filteredAccounts.length === 0 ? (
            <div className="empty-detail">
              <span>No matching accounts</span>
            </div>
          ) : (
            filteredAccounts.map((account) => {
              const stateLabel =
                account.state === 'active' ? null : (
                  <span className={`status-badge ${account.state === 'paused' ? 'warn' : 'err'}`}>{account.state.replace('_', ' ')}</span>
                );
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
                  className={`account-item${account.id === selectedAccountDetail?.id ? ' active' : ''}`}
                  onClick={() => onSelectDetail(account.id)}
                >
                  <div className="account-top">
                    <span className="account-name">{account.name}</span>
                    <div className="account-top-right">
                      <span className={`traffic-indicator${upActive || downActive ? ' active' : ''}`} aria-hidden="true">
                        <span className={`traffic-arrow up${upActive ? ' active' : ''}`}>↑</span>
                        <span className={`traffic-arrow down${downActive ? ' active' : ''}`}>↓</span>
                      </span>
                      <span className="account-plan">
                        {account.plan} {stateLabel}
                      </span>
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
                </button>
              );
            })
          )}
        </div>
      </div>
    </div>
  );
}
