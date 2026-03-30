import { describe, expect, test } from 'vitest';
import { accountsSeed, routingModesSeed, scoringModelSeed, stickySessionsSeed } from '../data/seed';
import type { ClusterStatus } from '../shared/types';
import { deriveMetrics, filteredRows, generateDeviceCode, paginateSessions, shouldScheduleAutoSync } from './logic';

describe('deriveMetrics', () => {
  test('marks blocked account with infinite score', () => {
    const accounts = accountsSeed.map((account) =>
      account.id === 'acc-overflow' ? { ...account, state: 'paused' as const } : { ...account },
    );

    const metrics = deriveMetrics(accounts, scoringModelSeed, 'power_of_two_choices', 0, routingModesSeed);

    expect(metrics.get('acc-overflow')?.capability).toBe(false);
    expect(metrics.get('acc-overflow')?.score).toBe(Infinity);
  });
});

describe('filteredRows', () => {
  test('filters by selected account and query', () => {
    const rows = [
      {
        time: '01:19:44',
        id: 'req_1',
        model: 'gpt-5.4',
        accountId: 'acc-alice',
        tokens: 1280,
        latency: 176,
        status: 'ok' as const,
        protocol: 'SSE' as const,
        sessionId: 'sess_1',
        sticky: true,
      },
      {
        time: '01:19:43',
        id: 'req_2',
        model: 'gpt-5.3-codex',
        accountId: 'acc-research',
        tokens: 940,
        latency: 248,
        status: 'ok' as const,
        protocol: 'WS' as const,
        sessionId: 'sess_2',
        sticky: false,
      },
    ];

    const results = filteredRows(rows, accountsSeed, 'acc-research', 'codex');
    expect(results).toHaveLength(1);
    expect(results[0]?.id).toBe('req_2');
  });
});

describe('paginateSessions', () => {
  test('returns paged sessions and stale count', () => {
    const view = paginateSessions(stickySessionsSeed, 'prompt_cache', 0, 2);
    expect(view.filtered.every((entry) => entry.kind === 'prompt_cache')).toBe(true);
    expect(view.paged).toHaveLength(2);
    expect(view.staleTotal).toBeGreaterThan(0);
  });
});

describe('generateDeviceCode', () => {
  test('is deterministic for a fixed seed and follows format', () => {
    const code = generateDeviceCode(123);
    expect(code).toBe(generateDeviceCode(123));
    expect(code).toMatch(/^[A-Z]{4}-[A-Z]{4}$/);
  });
});

describe('shouldScheduleAutoSync', () => {
  function status(overrides: Partial<ClusterStatus> = {}): ClusterStatus {
    return {
      enabled: true,
      site_id: '1',
      cluster_name: 'alpha',
      role: 'leader',
      term: 1,
      commit_index: 0,
      leader_id: '1',
      peers: [],
      journal_entries: 0,
      pending_raft_entries: 0,
      last_sync_at: null,
      ...overrides,
    };
  }

  test('returns false when there are no connected peers', () => {
    expect(shouldScheduleAutoSync(status(), 5)).toBe(false);
  });

  test('returns true when at least one peer is connected', () => {
    expect(
      shouldScheduleAutoSync(
        status({
          peers: [
            {
              site_id: '2',
              address: '10.0.0.2:9400',
              state: 'connected',
              role: 'follower',
              match_index: 0,
              last_heartbeat_at: null,
              discovered_via: 'manual',
            },
          ],
        }),
        5,
      ),
    ).toBe(true);
  });

  test('returns false when disabled or interval is non-positive', () => {
    expect(shouldScheduleAutoSync(status({ enabled: false }), 5)).toBe(false);
    expect(
      shouldScheduleAutoSync(
        status({
          peers: [
            {
              site_id: '2',
              address: '10.0.0.2:9400',
              state: 'connected',
              role: 'follower',
              match_index: 0,
              last_heartbeat_at: null,
              discovered_via: 'manual',
            },
          ],
        }),
        0,
      ),
    ).toBe(false);
  });
});
