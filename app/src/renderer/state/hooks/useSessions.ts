import { useCallback, useEffect, useRef, type Dispatch, type SetStateAction } from 'react';
import type { TightropeService } from '../../services/tightrope';
import type { AppRuntimeState, RuntimeStickySession } from '../../shared/types';

export interface UseSessionsOptions {
  refreshMs: number;
  sessionsRuntimeLimit: number;
  setState: Dispatch<SetStateAction<AppRuntimeState>>;
  mapRuntimeStickySession: (record: RuntimeStickySession, generatedAtMs: number) => AppRuntimeState['sessions'][number];
  clampSessionsOffset: (offset: number, totalCount: number) => number;
  listStickySessionsRequest: TightropeService['listStickySessionsRequest'];
  purgeStaleSessionsRequest: TightropeService['purgeStaleSessionsRequest'];
  reportPollingError?: (message: string) => void;
  reportPurgeError?: (message: string) => void;
}

interface UseSessionsResult {
  refreshStickySessions: () => Promise<void>;
  purgeStaleSessions: () => Promise<void>;
}

export function useSessions(options: UseSessionsOptions): UseSessionsResult {
  const { listStickySessionsRequest, purgeStaleSessionsRequest } = options;
  const refreshInFlightRef = useRef(false);
  const pollErrorReportedRef = useRef(false);

  const refreshStickySessions = useCallback(async (): Promise<void> => {
    if (refreshInFlightRef.current) {
      return;
    }

    refreshInFlightRef.current = true;
    try {
      const response = await listStickySessionsRequest(options.sessionsRuntimeLimit, 0);
      const generatedAtMs = Number.isFinite(response.generatedAtMs) ? Math.trunc(response.generatedAtMs) : Date.now();
      const sessions = Array.isArray(response.sessions)
        ? response.sessions
            .map((record) => options.mapRuntimeStickySession(record, generatedAtMs))
            .filter((session) => session.key !== '' && session.accountId !== '')
        : [];

      options.setState((previous) => {
        const filteredCount =
          previous.sessionsKindFilter === 'all'
            ? sessions.length
            : sessions.filter((session) => session.kind === previous.sessionsKindFilter).length;

        return {
          ...previous,
          sessions,
          sessionsOffset: options.clampSessionsOffset(previous.sessionsOffset, filteredCount),
        };
      });
      pollErrorReportedRef.current = false;
    } finally {
      refreshInFlightRef.current = false;
    }
  }, [listStickySessionsRequest, options]);

  const purgeStaleSessions = useCallback(async (): Promise<void> => {
    try {
      await purgeStaleSessionsRequest();
      await refreshStickySessions();
    } catch {
      options.reportPurgeError?.('sticky session purge failed');
    }
  }, [options, purgeStaleSessionsRequest, refreshStickySessions]);

  useEffect(() => {
    const handle = setInterval(() => {
      void refreshStickySessions().catch(() => {
        if (pollErrorReportedRef.current) {
          return;
        }
        pollErrorReportedRef.current = true;
        options.reportPollingError?.('sticky session polling failed; retrying');
      });
    }, options.refreshMs);

    return () => {
      clearInterval(handle);
    };
  }, [options.refreshMs, refreshStickySessions]);

  return { refreshStickySessions, purgeStaleSessions };
}
