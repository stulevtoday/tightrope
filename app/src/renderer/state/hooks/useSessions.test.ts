import { act, renderHook } from '@testing-library/react';
import { useState } from 'react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { createInitialRuntimeState } from '../../test/fixtures/seed';
import type { AppRuntimeState, RuntimeStickySession } from '../../shared/types';
import { useSessions } from './useSessions';

afterEach(() => {
  vi.useRealTimers();
});

describe('useSessions', () => {
  it('refreshes sticky sessions and clamps offset', async () => {
    const listStickySessions = vi.fn().mockResolvedValue({
      generatedAtMs: 1_710_000_000_000,
      sessions: [
        {
          sessionKey: 'turn_1',
          accountId: 'acc_1',
          updatedAtMs: 1_710_000_000_000,
          expiresAtMs: 1_710_000_100_000,
        } satisfies RuntimeStickySession,
      ],
    });
    const purgeStaleSessions = vi.fn(async () => ({ generatedAtMs: 1_710_000_000_000, purged: 0 }));

    const { result } = renderHook(() => {
      const [state, setState] = useState<AppRuntimeState>({
        ...createInitialRuntimeState(),
        sessionsOffset: 999,
      });

      const sessionsState = useSessions({
        refreshMs: 60_000,
        sessionsRuntimeLimit: 1000,
        setState,
        mapRuntimeStickySession: (record) => ({
          key: record.sessionKey,
          kind: 'codex_session',
          accountId: record.accountId,
          updated: '2026-04-02 12:00:00',
          expiry: '2026-04-02 12:01:40',
          stale: false,
        }),
        clampSessionsOffset: () => 0,
        listStickySessionsRequest: listStickySessions,
        purgeStaleSessionsRequest: purgeStaleSessions,
      });

      return { state, ...sessionsState };
    });

    await act(async () => {
      await result.current.refreshStickySessions();
    });

    expect(result.current.state.sessions).toHaveLength(1);
    expect(result.current.state.sessions[0].key).toBe('turn_1');
    expect(result.current.state.sessionsOffset).toBe(0);
  });

  it('reports polling error once until recovery', async () => {
    vi.useFakeTimers();
    const reportPollingError = vi.fn();
    const listStickySessions = vi.fn().mockRejectedValue(new Error('offline'));
    const purgeStaleSessions = vi.fn(async () => ({ generatedAtMs: Date.now(), purged: 0 }));

    const { result } = renderHook(() => {
      const [state, setState] = useState<AppRuntimeState>(createInitialRuntimeState());
      return useSessions({
        refreshMs: 10,
        sessionsRuntimeLimit: 1000,
        setState,
        mapRuntimeStickySession: (record) => ({
          key: record.sessionKey,
          kind: 'codex_session',
          accountId: record.accountId,
          updated: '2026-04-02 12:00:00',
          expiry: null,
          stale: false,
        }),
        clampSessionsOffset: (offset) => offset,
        reportPollingError,
        listStickySessionsRequest: listStickySessions,
        purgeStaleSessionsRequest: purgeStaleSessions,
      });
    });

    await act(async () => {
      vi.advanceTimersByTime(30);
      await Promise.resolve();
    });

    expect(reportPollingError).toHaveBeenCalledTimes(1);

    await act(async () => {
      listStickySessions.mockResolvedValueOnce({ generatedAtMs: Date.now(), sessions: [] });
      await result.current.refreshStickySessions();
      listStickySessions.mockRejectedValueOnce(new Error('offline'));
    });

    await act(async () => {
      vi.advanceTimersByTime(10);
      await Promise.resolve();
    });

    expect(reportPollingError).toHaveBeenCalledTimes(2);
  });

  it('purges stale sessions through the service and refreshes the list', async () => {
    const listStickySessions = vi.fn().mockResolvedValue({ generatedAtMs: Date.now(), sessions: [] });
    const purgeStaleSessions = vi.fn(async () => ({ generatedAtMs: Date.now(), purged: 2 }));

    const { result } = renderHook(() => {
      const [, setState] = useState<AppRuntimeState>(createInitialRuntimeState());
      return useSessions({
        refreshMs: 60_000,
        sessionsRuntimeLimit: 1000,
        setState,
        mapRuntimeStickySession: (record) => ({
          key: record.sessionKey,
          kind: 'codex_session',
          accountId: record.accountId,
          updated: '2026-04-02 12:00:00',
          expiry: null,
          stale: false,
        }),
        clampSessionsOffset: (offset) => offset,
        listStickySessionsRequest: listStickySessions,
        purgeStaleSessionsRequest: purgeStaleSessions,
      });
    });

    await act(async () => {
      await result.current.purgeStaleSessions();
    });

    expect(purgeStaleSessions).toHaveBeenCalledTimes(1);
    expect(listStickySessions).toHaveBeenCalledTimes(1);
  });
});
