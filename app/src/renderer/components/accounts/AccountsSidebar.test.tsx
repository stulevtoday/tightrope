import { render, screen } from '@testing-library/react';
import { describe, expect, test, vi } from 'vitest';
import type { Account } from '../../shared/types';
import type { AccountSessionSummary } from '../shared/CollaborationStatusPanel';
import { AccountsSidebar } from './AccountsSidebar';

function accountFixture(overrides: Partial<Account> = {}): Account {
  return {
    id: 'acc_1',
    name: 'one@test.local',
    pinned: false,
    plan: 'plus',
    health: 'healthy',
    state: 'active',
    inflight: 0,
    load: 0,
    latency: 0,
    errorEwma: 0,
    cooldown: false,
    capability: true,
    costNorm: 0,
    routed24h: 0,
    stickyHit: 0,
    quotaPrimary: 20,
    quotaSecondary: 10,
    hasPrimaryQuota: true,
    hasSecondaryQuota: true,
    quotaPrimaryWindowSeconds: 18_000,
    quotaSecondaryWindowSeconds: 604_800,
    quotaPrimaryResetAtMs: Date.now() + 3_600_000,
    quotaSecondaryResetAtMs: Date.now() + 86_400_000,
    failovers: 0,
    note: 'openai',
    telemetryBacked: true,
    trafficUpBytes: 0,
    trafficDownBytes: 0,
    trafficLastUpAtMs: 0,
    trafficLastDownAtMs: 0,
    ...overrides,
  };
}

function renderSidebar(
  accounts: Account[],
  trafficNowMs = Date.now(),
  routedAccountId: string | null = null,
  accountSessionSummaries = new Map<string, AccountSessionSummary>(),
) {
  return render(
    <AccountsSidebar
      filteredAccounts={accounts}
      totalAccounts={accounts.length}
      selectedAccountDetail={null}
      routedAccountId={routedAccountId}
      accountSessionSummaries={accountSessionSummaries}
      trafficNowMs={trafficNowMs}
      accountSearchQuery=""
      accountStatusFilter=""
      isRefreshingAllTelemetry={false}
      onOpenAddAccount={vi.fn()}
      onRefreshAllTelemetry={vi.fn(async () => {})}
      onSearch={vi.fn()}
      onFilterStatus={vi.fn()}
      onSelectDetail={vi.fn()}
    />,
  );
}

function accountListOrder(container: HTMLElement): string[] {
  return Array.from(container.querySelectorAll('.account-item .account-name')).map((element) => element.textContent?.trim() ?? '');
}

describe('AccountsSidebar attention indicator', () => {
  test('shows an amber attention sign when token refresh is required', () => {
    renderSidebar([
      accountFixture({ id: 'acc_ok', name: 'ok@test.local' }),
      accountFixture({ id: 'acc_warn', name: 'warn@test.local', needsTokenRefresh: true }),
    ]);

    const indicators = screen.getAllByLabelText(/Needs attention:/i);
    expect(indicators).toHaveLength(1);
    expect(indicators[0]).toHaveAttribute('title', 'Token refresh required');
  });

  test('shows an amber attention sign from stored auth-required refresh status', () => {
    renderSidebar([accountFixture({
      id: 'acc_auth',
      name: 'auth@test.local',
      usageRefreshStatus: 'auth_required',
      usageRefreshMessage: 'code=token_expired',
    })]);

    expect(screen.getByLabelText(/Needs attention: Token refresh required/i)).toBeInTheDocument();
  });

  test('does not show attention sign for quota or rate-limit state alone', () => {
    renderSidebar([
      accountFixture({ id: 'acc_hot', name: 'hot@test.local', quotaPrimary: 99 }),
      accountFixture({ id: 'acc_rate', name: 'rate@test.local', state: 'rate_limited' }),
    ]);

    expect(screen.queryByLabelText(/Needs attention:/i)).not.toBeInTheDocument();
  });

  test('shows weekly countdown alongside short-window countdown for paid accounts', () => {
    const nowMs = 1_700_000_000_000;
    renderSidebar(
      [
        accountFixture({
          id: 'acc_weekly',
          name: 'weekly@test.local',
          quotaPrimary: 40,
          quotaSecondary: 15,
          hasSecondaryQuota: true,
          quotaPrimaryResetAtMs: nowMs + 2 * 60 * 60 * 1000,
          quotaSecondaryResetAtMs: nowMs + 3 * 24 * 60 * 60 * 1000,
        }),
      ],
      nowMs,
    );

    const row = screen.getByText('weekly@test.local').closest('.account-item');
    expect(row).not.toBeNull();
    expect(row).toHaveTextContent(/5-hour left/i);
    expect(row).toHaveTextContent(/Weekly left/i);
  });

  test('moves the currently routed account to the top without changing the rest of the list', () => {
    const { container } = renderSidebar(
      [
        accountFixture({ id: 'acc_alpha', name: 'alpha@test.local' }),
        accountFixture({ id: 'acc_bravo', name: 'bravo@test.local' }),
        accountFixture({ id: 'acc_charlie', name: 'charlie@test.local' }),
      ],
      Date.now(),
      'acc_charlie',
    );

    expect(accountListOrder(container)).toEqual([
      'charlie@test.local',
      'alpha@test.local',
      'bravo@test.local',
    ]);
  });

  test('shows a session chip for accounts with active pinned sessions', () => {
    const { container } = renderSidebar(
      [accountFixture({ id: 'acc_active', name: 'active@test.local' })],
      Date.now(),
      null,
      new Map([
        [
          'acc_active',
          {
            active: 2,
            stale: 0,
            codex: 2,
            sticky: 0,
            promptCache: 0,
          },
        ],
      ]),
    );

    const chip = container.querySelector('.account-session-chip.parallel');
    expect(chip).not.toBeNull();
    expect(chip).toHaveTextContent('2');
    expect(chip).toHaveAttribute('title', '2 Codex sessions are pinned to this account');
  });
});
