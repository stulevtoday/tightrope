import type { Account } from '../../shared/types';
import { AccountDetailPanel } from './AccountDetailPanel';
import { AccountsSidebar } from './AccountsSidebar';

interface AccountsPageProps {
  visible: boolean;
  accounts: Account[];
  filteredAccounts: Account[];
  selectedAccountDetail: Account | null;
  accountSearchQuery: string;
  accountStatusFilter: '' | 'active' | 'paused' | 'rate_limited' | 'deactivated' | 'quota_blocked';
  onOpenAddAccount: () => void;
  onSearch: (query: string) => void;
  onFilterStatus: (status: '' | 'active' | 'paused' | 'rate_limited' | 'deactivated' | 'quota_blocked') => void;
  onSelectDetail: (accountId: string) => void;
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

export function AccountsPage({
  visible,
  accounts,
  filteredAccounts,
  selectedAccountDetail,
  accountSearchQuery,
  accountStatusFilter,
  onOpenAddAccount,
  onSearch,
  onFilterStatus,
  onSelectDetail,
  stableSparklinePercents,
  deterministicAccountDetailValues,
  formatNumber,
  isRefreshingUsageTelemetry,
  onRefreshUsageTelemetry,
  onPauseAccount,
  onReactivateAccount,
  onDeleteAccount,
}: AccountsPageProps) {
  if (!visible) return null;

  return (
    <section className="accounts-page page active" id="pageAccounts" data-page="accounts">
      <AccountsSidebar
        filteredAccounts={filteredAccounts}
        selectedAccountDetail={selectedAccountDetail}
        accountSearchQuery={accountSearchQuery}
        accountStatusFilter={accountStatusFilter}
        onOpenAddAccount={onOpenAddAccount}
        onSearch={onSearch}
        onFilterStatus={onFilterStatus}
        onSelectDetail={onSelectDetail}
      />
      <AccountDetailPanel
        selectedAccountDetail={selectedAccountDetail}
        stableSparklinePercents={stableSparklinePercents}
        deterministicAccountDetailValues={deterministicAccountDetailValues}
        formatNumber={formatNumber}
        isRefreshingUsageTelemetry={isRefreshingUsageTelemetry}
        onRefreshUsageTelemetry={onRefreshUsageTelemetry}
        onPauseAccount={onPauseAccount}
        onReactivateAccount={onReactivateAccount}
        onDeleteAccount={onDeleteAccount}
      />
    </section>
  );
}
