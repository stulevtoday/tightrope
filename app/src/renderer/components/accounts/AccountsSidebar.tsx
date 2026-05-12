import i18next from 'i18next';
import { useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import type { Account } from '../../shared/types';
import { AccountSessionChip, type AccountSessionSummary } from '../shared/CollaborationStatusPanel';

interface AccountsSidebarProps {
  filteredAccounts: Account[];
  totalAccounts: number;
  selectedAccountDetail: Account | null;
  routedAccountId?: string | null;
  accountSessionSummaries?: Map<string, AccountSessionSummary>;
  trafficNowMs: number;
  accountSearchQuery: string;
  accountStatusFilter: '' | 'active' | 'paused' | 'rate_limited' | 'deactivated' | 'quota_blocked';
  isRefreshingAllTelemetry: boolean;
  onOpenAddAccount: () => void;
  onRefreshAllTelemetry: () => Promise<void>;
  onSearch: (query: string) => void;
  onFilterStatus: (status: '' | 'active' | 'paused' | 'rate_limited' | 'deactivated' | 'quota_blocked') => void;
  onSelectDetail: (accountId: string) => void;
}

const MAX_UNIX_SECONDS_BEFORE_MS = 10_000_000_000;

function clampPercent(value: number): number {
  if (!Number.isFinite(value)) {
    return 0;
  }
  return Math.min(100, Math.max(0, Math.round(value)));
}

function primaryQuotaWindowLabel(account: Account): string {
  const windowSeconds = account.quotaPrimaryWindowSeconds;
  if (typeof windowSeconds === 'number' && Number.isFinite(windowSeconds) && windowSeconds > 0) {
    if (windowSeconds <= 6 * 60 * 60) {
      return i18next.t('common.hour_window', { hours: 5 });
    }
    if (windowSeconds >= 6 * 24 * 60 * 60) {
      return i18next.t('common.weekly');
    }
    if (windowSeconds >= 20 * 60 * 60 && windowSeconds <= 28 * 60 * 60) {
      return i18next.t('common.daily');
    }
    const roundedHours = Math.round(windowSeconds / (60 * 60));
    if (roundedHours >= 1 && roundedHours <= 23) {
      return i18next.t('common.hour_window', { hours: roundedHours });
    }
    const roundedDays = Math.round(windowSeconds / (24 * 60 * 60));
    if (roundedDays >= 2) {
      return i18next.t('common.day_window', { days: roundedDays });
    }
  }
  return account.plan === 'free' ? i18next.t('common.weekly') : i18next.t('common.hour_window', { hours: 5 });
}

function normalizeResetAtMs(value: number | null | undefined): number | null {
  if (typeof value !== 'number' || !Number.isFinite(value) || value <= 0) {
    return null;
  }
  if (value < MAX_UNIX_SECONDS_BEFORE_MS) {
    return Math.trunc(value * 1000);
  }
  return Math.trunc(value);
}

function formatResetCountdown(resetAtMs: number | null, nowMs: number): string | null {
  if (resetAtMs === null || !Number.isFinite(resetAtMs) || resetAtMs <= 0) {
    return null;
  }
  const remainingMs = Math.max(0, resetAtMs - nowMs);
  const totalMinutes = Math.ceil(remainingMs / (60 * 1000));
  if (totalMinutes <= 0) {
    return i18next.t('common.now');
  }
  const days = Math.floor(totalMinutes / (24 * 60));
  const hours = Math.floor((totalMinutes % (24 * 60)) / 60);
  const minutes = totalMinutes % 60;
  if (days > 0) {
    if (hours > 0) {
      return i18next.t('common.countdown_d_h_m', { days, hours, minutes });
    }
    return i18next.t('common.countdown_d_h_m', { days, hours: 0, minutes });
  }
  if (hours > 0) {
    return i18next.t('common.countdown_h_m', { hours, minutes });
  }
  return i18next.t('common.countdown_m', { minutes });
}

function hasSupplementaryWeeklyQuota(account: Account): boolean {
  const hasWindowSeconds =
    typeof account.quotaSecondaryWindowSeconds === 'number' &&
    Number.isFinite(account.quotaSecondaryWindowSeconds) &&
    account.quotaSecondaryWindowSeconds > 0;
  return (
    account.plan !== 'free' &&
    (account.hasSecondaryQuota === true || hasWindowSeconds || normalizeResetAtMs(account.quotaSecondaryResetAtMs) !== null)
  );
}

function planPrimaryResetAtMs(account: Account): number | null {
  const primaryReset = normalizeResetAtMs(account.quotaPrimaryResetAtMs);
  const secondaryReset = normalizeResetAtMs(account.quotaSecondaryResetAtMs);
  if (account.plan === 'free') {
    return secondaryReset ?? primaryReset;
  }
  return primaryReset ?? secondaryReset;
}

function supplementaryWeeklyResetAtMs(account: Account): number | null {
  if (!hasSupplementaryWeeklyQuota(account)) {
    return null;
  }
  return normalizeResetAtMs(account.quotaSecondaryResetAtMs);
}

function formatRemainingLabel(remainingPercent: number | null, resetAtMs: number | null, nowMs: number): string {
  if (remainingPercent === null) {
    return '—';
  }
  const countdown = formatResetCountdown(resetAtMs, nowMs);
  return countdown ? `${remainingPercent}% (${countdown})` : `${remainingPercent}%`;
}

function accountStateBadgeClass(state: Account['state']): string {
  switch (state) {
    case 'paused':
      return 'warn';
    case 'rate_limited':
      return 'rate-limited';
    case 'quota_blocked':
      return 'quota-blocked';
    case 'deactivated':
      return 'error';
    default:
      return 'error';
  }
}

function accountAttentionReason(account: Account): string | null {
  if (account.usageRefreshStatus === 'auth_required' || account.needsTokenRefresh === true) {
    return i18next.t('accounts.sidebar_token_refresh_required');
  }

  return null;
}

export function AccountsSidebar({
  filteredAccounts,
  totalAccounts,
  selectedAccountDetail,
  routedAccountId = null,
  accountSessionSummaries = new Map<string, AccountSessionSummary>(),
  trafficNowMs,
  accountSearchQuery,
  accountStatusFilter,
  isRefreshingAllTelemetry,
  onOpenAddAccount,
  onRefreshAllTelemetry,
  onSearch,
  onFilterStatus,
  onSelectDetail,
}: AccountsSidebarProps) {
  const { t } = useTranslation();
  const sortedAccounts = useMemo(() => {
    if (!routedAccountId || !filteredAccounts.some((account) => account.id === routedAccountId)) {
      return filteredAccounts;
    }
    return [...filteredAccounts].sort((left, right) => {
      const leftRouted = left.id === routedAccountId;
      const rightRouted = right.id === routedAccountId;
      if (leftRouted !== rightRouted) {
        return leftRouted ? -1 : 1;
      }
      return 0;
    });
  }, [filteredAccounts, routedAccountId]);

  return (
    <div className="accounts-sidebar">
      <header className="section-header">
        <div>
          <p className="eyebrow">{t('accounts.sidebar_eyebrow')}</p>
          <h2>{t('accounts.sidebar_title')}</h2>
        </div>
        <div className="accounts-header-actions">
          <button
            className="tool-btn"
            type="button"
            disabled={isRefreshingAllTelemetry || totalAccounts === 0}
            onClick={() => {
              void onRefreshAllTelemetry();
            }}
          >
            {isRefreshingAllTelemetry ? t('accounts.sidebar_refreshing') : t('accounts.sidebar_refresh_all')}
          </button>
          <button className="tool-btn" type="button" onClick={onOpenAddAccount}>
            {t('accounts.sidebar_add')}
          </button>
        </div>
      </header>
      <div className="accounts-filter-bar">
        <input
          className="search"
          type="search"
          placeholder={t('accounts.sidebar_search_placeholder')}
          aria-label={t('accounts.sidebar_search_label')}
          value={accountSearchQuery}
          onChange={(event) => onSearch(event.target.value)}
        />
        <select value={accountStatusFilter} aria-label={t('accounts.sidebar_filter_label')} onChange={(event) => onFilterStatus(event.target.value as AccountsSidebarProps['accountStatusFilter'])}>
          <option value="">{t('accounts.sidebar_filter_all')}</option>
          <option value="active">{t('accounts.sidebar_filter_active')}</option>
          <option value="paused">{t('accounts.sidebar_filter_paused')}</option>
          <option value="rate_limited">{t('accounts.sidebar_filter_rate_limited')}</option>
          <option value="quota_blocked">{t('accounts.sidebar_filter_quota_blocked')}</option>
          <option value="deactivated">{t('accounts.sidebar_filter_deactivated')}</option>
        </select>
      </div>
      <div className="pane-body">
        <div className="accounts-list">
          {sortedAccounts.length === 0 ? (
            <div className="empty-detail">
              <span>{t('accounts.sidebar_no_matching')}</span>
            </div>
          ) : (
            sortedAccounts.map((account) => {
              const attentionReason = accountAttentionReason(account);
              const isRouted = account.id === routedAccountId;
              const accountSessionSummary = accountSessionSummaries.get(account.id);
              const hasSessionChip = (accountSessionSummary?.active ?? 0) > 0;
              const stateLabel =
                account.state === 'active' ? null : (
                  <span className={`status-badge ${accountStateBadgeClass(account.state)}`}>{t(`common.state_${account.state}`)}</span>
                );
              const primaryUsage = account.telemetryBacked ? clampPercent(account.quotaPrimary) : null;
              const primaryRemaining = primaryUsage === null ? 0 : Math.max(0, 100 - primaryUsage);
              const secondaryUsage =
                account.telemetryBacked && account.hasSecondaryQuota ? clampPercent(account.quotaSecondary) : null;
              const secondaryRemaining = secondaryUsage === null ? 0 : Math.max(0, 100 - secondaryUsage);
              const primaryWindowLabel = primaryQuotaWindowLabel(account);
              const primaryRemainingLabel = formatRemainingLabel(
                primaryUsage === null ? null : primaryRemaining,
                planPrimaryResetAtMs(account),
                trafficNowMs,
              );
              const weeklyRemainingLabel = primaryWindowLabel === t('common.hour_window', { hours: 5 }) && hasSupplementaryWeeklyQuota(account)
                ? formatRemainingLabel(
                    secondaryUsage === null ? null : secondaryRemaining,
                    supplementaryWeeklyResetAtMs(account),
                    trafficNowMs,
                  )
                : null;
              return (
                <div
                  key={account.id}
                  className={`account-item${account.id === selectedAccountDetail?.id ? ' active' : ''}${isRouted ? ' routed' : ''}${attentionReason ? ' needs-attention' : ''}`}
                  role="button"
                  tabIndex={0}
                  onClick={() => onSelectDetail(account.id)}
                  onKeyDown={(event) => {
                    if (event.key === 'Enter' || event.key === ' ') {
                      event.preventDefault();
                      onSelectDetail(account.id);
                    }
                  }}
                >
                  <div className="account-top">
                    <span className="account-name" title={account.name}>
                      {account.name}
                    </span>
                    {attentionReason || isRouted || hasSessionChip ? (
                      <span className="account-top-right">
                        <AccountSessionChip summary={accountSessionSummary} />
                        {isRouted ? <span className="routed-dot routed-dot-subtle" title={t('router.pool_routed_account')} aria-hidden="true" /> : null}
                        {attentionReason ? (
                          <span
                            className="attention-sign"
                            role="img"
                            aria-label={t('accounts.sidebar_needs_attention', { reason: attentionReason })}
                            title={attentionReason}
                          >
                            <span className="attention-sign-mark">!</span>
                          </span>
                        ) : null}
                      </span>
                    ) : null}
                  </div>
                  <div className="account-status-row">
                    <span className="account-status-meta">
                      <span className="account-plan">{account.plan}</span>
                      {stateLabel}
                    </span>
                  </div>
                  <div className="account-meta-row">
                    <span className="account-meta">
                      {primaryWindowLabel} {t('router.pool_left')} <strong>{primaryRemainingLabel}</strong>
                    </span>
                  </div>
                  {weeklyRemainingLabel !== null ? (
                    <div className="account-meta-row account-meta-row-secondary">
                      <span className="account-meta">
                        {t('router.pool_weekly_left')} <strong>{weeklyRemainingLabel}</strong>
                      </span>
                    </div>
                  ) : null}
                  <div className="quota-stack">
                    <div className="mini-bar quota-track" aria-label={t('router.pool_quota_remaining', { window: primaryWindowLabel })}>
                      <div
                        className={`mini-fill quota-fill${primaryUsage !== null && primaryUsage >= 80 ? ' hot' : ''}`}
                        style={{ width: `${primaryRemaining}%` }}
                      />
                    </div>
                    {secondaryUsage !== null ? (
                      <div className="mini-bar quota-track quota-secondary-track" aria-label={t('router.pool_weekly_quota_remaining')}>
                        <div
                          className={`mini-fill quota-fill quota-fill-secondary${secondaryUsage >= 80 ? ' hot' : ''}`}
                          style={{ width: `${secondaryRemaining}%` }}
                        />
                      </div>
                    ) : null}
                  </div>
                </div>
              );
            })
          )}
        </div>
      </div>
    </div>
  );
}
