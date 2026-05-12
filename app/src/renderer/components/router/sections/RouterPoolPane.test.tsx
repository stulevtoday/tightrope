import { act, render, within, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { describe, expect, test, vi } from 'vitest';
import type { Account, RouteMetrics } from '../../../shared/types';
import { RouterPoolPane } from './RouterPoolPane';

function makeAccount(id: string, name: string, overrides: Partial<Account> = {}): Account {
  return {
    id,
    name,
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
    quotaPrimary: 0,
    quotaSecondary: 0,
    hasPrimaryQuota: false,
    hasSecondaryQuota: false,
    quotaPrimaryWindowSeconds: null,
    quotaSecondaryWindowSeconds: null,
    quotaPrimaryResetAtMs: null,
    quotaSecondaryResetAtMs: null,
    failovers: 0,
    note: '',
    telemetryBacked: false,
    ...overrides,
  };
}

function accountRowByName(name: string): HTMLElement {
  const rowLabel = Array.from(document.querySelectorAll('.account-item .account-name')).find(
    (element) => element.textContent?.trim() === name,
  );
  const row = rowLabel?.closest('.account-item');
  if (!(row instanceof HTMLElement)) {
    throw new Error(`Account row not found for ${name}`);
  }
  return row;
}

function lockButtonForAccount(name: string): HTMLElement {
  return within(accountRowByName(name)).getByRole('button', { name: /toggle routing lock group/i });
}

function accountListOrder(container: HTMLElement): string[] {
  return Array.from(container.querySelectorAll('.account-item .account-name')).map((element) => element.textContent?.trim() ?? '');
}

describe('RouterPoolPane lock selection', () => {
  test('resetting soon sort prioritizes weekly reset then short-window reset', () => {
    const nowMs = 1_700_000_000_000;
    const accounts = [
      makeAccount('acc_a', 'alpha@test.local', {
        plan: 'plus',
        telemetryBacked: true,
        hasPrimaryQuota: true,
        hasSecondaryQuota: true,
        quotaPrimaryWindowSeconds: 18_000,
        quotaSecondaryWindowSeconds: 604_800,
        quotaPrimaryResetAtMs: nowMs + 4 * 60 * 60 * 1000,
        quotaSecondaryResetAtMs: nowMs + 2 * 60 * 60 * 1000,
      }),
      makeAccount('acc_b', 'bravo@test.local', {
        plan: 'plus',
        telemetryBacked: true,
        hasPrimaryQuota: true,
        hasSecondaryQuota: true,
        quotaPrimaryWindowSeconds: 18_000,
        quotaSecondaryWindowSeconds: 604_800,
        quotaPrimaryResetAtMs: nowMs + 10 * 60 * 1000,
        quotaSecondaryResetAtMs: nowMs + 3 * 60 * 60 * 1000,
      }),
      makeAccount('acc_c', 'charlie@test.local', {
        plan: 'free',
        telemetryBacked: true,
        hasPrimaryQuota: false,
        hasSecondaryQuota: true,
        quotaPrimaryWindowSeconds: null,
        quotaSecondaryWindowSeconds: 604_800,
        quotaPrimaryResetAtMs: null,
        quotaSecondaryResetAtMs: nowMs + 150 * 60 * 1000,
      }),
      makeAccount('acc_d', 'delta@test.local', {
        plan: 'plus',
        telemetryBacked: true,
        hasPrimaryQuota: true,
        hasSecondaryQuota: true,
        quotaPrimaryWindowSeconds: 18_000,
        quotaSecondaryWindowSeconds: 604_800,
        quotaPrimaryResetAtMs: nowMs + 30 * 60 * 1000,
        quotaSecondaryResetAtMs: nowMs + 2 * 60 * 60 * 1000,
      }),
    ];

    const { container } = render(
      <RouterPoolPane
        accounts={accounts}
        metrics={new Map<string, RouteMetrics>()}
        routedAccountId={null}
        lockedRoutingAccountIds={[]}
        recentRouteActivityByAccount={new Map<string, number>()}
        trafficNowMs={nowMs}
        trafficActiveWindowMs={30_000}
        selectedAccountId="acc_a"
        onSelectAccount={vi.fn()}
        onTogglePin={vi.fn()}
        onUpdateLockedRoutingAccountIds={vi.fn(async () => true)}
        onOpenAddAccount={vi.fn()}
      />,
    );

    expect(accountListOrder(container)).toEqual([
      'delta@test.local',
      'alpha@test.local',
      'charlie@test.local',
      'bravo@test.local',
    ]);
  });

  test('shows weekly reset countdown alongside short-window countdown for paid accounts', () => {
    const nowMs = 1_700_000_000_000;
    render(
      <RouterPoolPane
        accounts={[
          makeAccount('acc_plus', 'plus@test.local', {
            plan: 'plus',
            telemetryBacked: true,
            hasPrimaryQuota: true,
            hasSecondaryQuota: true,
            quotaPrimary: 35,
            quotaSecondary: 20,
            quotaPrimaryWindowSeconds: 18_000,
            quotaSecondaryWindowSeconds: 604_800,
            quotaPrimaryResetAtMs: nowMs + 2 * 60 * 60 * 1000,
            quotaSecondaryResetAtMs: nowMs + 3 * 24 * 60 * 60 * 1000,
          }),
        ]}
        metrics={new Map<string, RouteMetrics>()}
        routedAccountId={null}
        lockedRoutingAccountIds={[]}
        recentRouteActivityByAccount={new Map<string, number>()}
        trafficNowMs={nowMs}
        trafficActiveWindowMs={30_000}
        selectedAccountId="acc_plus"
        onSelectAccount={vi.fn()}
        onTogglePin={vi.fn()}
        onUpdateLockedRoutingAccountIds={vi.fn(async () => true)}
        onOpenAddAccount={vi.fn()}
      />,
    );

    const row = accountRowByName('plus@test.local');
    expect(row).toHaveTextContent(/5-hour left/i);
    expect(row).toHaveTextContent(/Weekly left/i);
  });

  test('moves the currently routed account above the configured sort order', () => {
    const nowMs = 1_700_000_000_000;
    const accounts = [
      makeAccount('acc_alpha', 'alpha@test.local', {
        telemetryBacked: true,
        hasPrimaryQuota: true,
        quotaPrimaryResetAtMs: nowMs + 10 * 60 * 1000,
      }),
      makeAccount('acc_bravo', 'bravo@test.local', {
        telemetryBacked: true,
        hasPrimaryQuota: true,
        quotaPrimaryResetAtMs: nowMs + 3 * 60 * 60 * 1000,
      }),
      makeAccount('acc_charlie', 'charlie@test.local', {
        telemetryBacked: true,
        hasPrimaryQuota: true,
        quotaPrimaryResetAtMs: nowMs + 20 * 60 * 1000,
      }),
    ];

    const { container } = render(
      <RouterPoolPane
        accounts={accounts}
        metrics={new Map<string, RouteMetrics>()}
        routedAccountId="acc_bravo"
        lockedRoutingAccountIds={[]}
        recentRouteActivityByAccount={new Map<string, number>()}
        trafficNowMs={nowMs}
        trafficActiveWindowMs={30_000}
        selectedAccountId="acc_alpha"
        onSelectAccount={vi.fn()}
        onTogglePin={vi.fn()}
        onUpdateLockedRoutingAccountIds={vi.fn(async () => true)}
        onOpenAddAccount={vi.fn()}
      />,
    );

    expect(accountListOrder(container)).toEqual([
      'bravo@test.local',
      'alpha@test.local',
      'charlie@test.local',
    ]);
  });

  test(
    'delays moving a newly formed lock group to the top by 5 seconds',
    async () => {
      const user = userEvent.setup();
      const onUpdateLockedRoutingAccountIds = vi.fn(async () => true);
      const accounts = [
        makeAccount('acc-top', 'alpha@test.local'),
        makeAccount('acc-middle', 'bravo@test.local'),
        makeAccount('acc-bottom', 'charlie@test.local'),
      ];

      const { container } = render(
        <RouterPoolPane
          accounts={accounts}
          metrics={new Map<string, RouteMetrics>()}
          routedAccountId={null}
          lockedRoutingAccountIds={[]}
          recentRouteActivityByAccount={new Map<string, number>()}
          trafficNowMs={Date.now()}
          trafficActiveWindowMs={30_000}
          selectedAccountId="acc-top"
          onSelectAccount={vi.fn()}
          onTogglePin={vi.fn()}
          onUpdateLockedRoutingAccountIds={onUpdateLockedRoutingAccountIds}
          onOpenAddAccount={vi.fn()}
        />,
      );

      await user.click(lockButtonForAccount('alpha@test.local'));
      await user.keyboard('[ShiftLeft>]');
      await user.click(lockButtonForAccount('charlie@test.local'));
      await user.keyboard('[/ShiftLeft]');

      expect(lockButtonForAccount('alpha@test.local')).toHaveClass('lock-grouped');
      expect(lockButtonForAccount('charlie@test.local')).toHaveClass('lock-grouped');
      expect(lockButtonForAccount('bravo@test.local')).not.toHaveClass('lock-grouped');
      expect(onUpdateLockedRoutingAccountIds).toHaveBeenLastCalledWith(['acc-top', 'acc-bottom']);

      expect(accountListOrder(container)).toEqual(['alpha@test.local', 'bravo@test.local', 'charlie@test.local']);

      await act(async () => {
        await new Promise((resolve) => window.setTimeout(resolve, 5_200));
      });
      expect(accountListOrder(container)).toEqual(['alpha@test.local', 'charlie@test.local', 'bravo@test.local']);
    },
    15_000,
  );

  test('keeps non-adjacent shift selections from an existing lock group via shift-toggle', async () => {
    const user = userEvent.setup();
    const onUpdateLockedRoutingAccountIds = vi.fn(async () => true);
    const accounts = [
      makeAccount('acc-top', 'alpha@test.local'),
      makeAccount('acc-middle', 'bravo@test.local'),
      makeAccount('acc-bottom', 'charlie@test.local'),
    ];

    render(
      <RouterPoolPane
        accounts={accounts}
        metrics={new Map<string, RouteMetrics>()}
        routedAccountId={null}
        lockedRoutingAccountIds={['acc-top', 'acc-middle']}
        recentRouteActivityByAccount={new Map<string, number>()}
        trafficNowMs={Date.now()}
        trafficActiveWindowMs={30_000}
        selectedAccountId="acc-top"
        onSelectAccount={vi.fn()}
        onTogglePin={vi.fn()}
        onUpdateLockedRoutingAccountIds={onUpdateLockedRoutingAccountIds}
        onOpenAddAccount={vi.fn()}
      />,
    );

    await user.keyboard('[ShiftLeft>]');
    await user.click(lockButtonForAccount('bravo@test.local'));
    await user.click(lockButtonForAccount('charlie@test.local'));
    await user.keyboard('[/ShiftLeft]');

    await waitFor(() => {
      expect(lockButtonForAccount('alpha@test.local')).toHaveClass('lock-grouped');
      expect(lockButtonForAccount('charlie@test.local')).toHaveClass('lock-grouped');
      expect(lockButtonForAccount('bravo@test.local')).not.toHaveClass('lock-grouped');
      expect(onUpdateLockedRoutingAccountIds).toHaveBeenLastCalledWith(['acc-top', 'acc-bottom']);
    });
  });

  test('removing one lock from a multi-account group keeps the remaining locks', async () => {
    const user = userEvent.setup();
    const onUpdateLockedRoutingAccountIds = vi.fn(async () => true);
    const accounts = [
      makeAccount('acc-top', 'alpha@test.local'),
      makeAccount('acc-middle', 'bravo@test.local'),
      makeAccount('acc-bottom', 'charlie@test.local'),
    ];

    render(
      <RouterPoolPane
        accounts={accounts}
        metrics={new Map<string, RouteMetrics>()}
        routedAccountId={null}
        lockedRoutingAccountIds={['acc-top', 'acc-middle', 'acc-bottom']}
        recentRouteActivityByAccount={new Map<string, number>()}
        trafficNowMs={Date.now()}
        trafficActiveWindowMs={30_000}
        selectedAccountId="acc-top"
        onSelectAccount={vi.fn()}
        onTogglePin={vi.fn()}
        onUpdateLockedRoutingAccountIds={onUpdateLockedRoutingAccountIds}
        onOpenAddAccount={vi.fn()}
      />,
    );

    await user.click(lockButtonForAccount('bravo@test.local'));

    await waitFor(() => {
      expect(lockButtonForAccount('alpha@test.local')).toHaveClass('lock-grouped');
      expect(lockButtonForAccount('charlie@test.local')).toHaveClass('lock-grouped');
      expect(lockButtonForAccount('bravo@test.local')).not.toHaveClass('lock-grouped');
      expect(onUpdateLockedRoutingAccountIds).toHaveBeenLastCalledWith(['acc-top', 'acc-bottom']);
    });
  });
});
