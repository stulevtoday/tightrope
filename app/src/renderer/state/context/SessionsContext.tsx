import { createContext, useContext } from 'react';
import type { StickySession } from '../../shared/types';

export interface SessionsContextValue {
  sessions: StickySession[];
  sessionsKindFilter: StickySession['kind'] | 'all';
  sessionsView: { filtered: StickySession[]; paged: StickySession[]; staleTotal: number };
  sessionsPaginationLabel: string;
  canPrevSessions: boolean;
  canNextSessions: boolean;
  setSessionsKindFilter: (kind: StickySession['kind'] | 'all') => void;
  prevSessionsPage: () => void;
  nextSessionsPage: () => void;
  purgeStaleSessions: () => void;
}

export const SessionsContext = createContext<SessionsContextValue | null>(null);

export function useSessionsContext(): SessionsContextValue {
  const context = useContext(SessionsContext);
  if (!context) {
    throw new Error('useSessionsContext must be used within AppStateProviders');
  }
  return context;
}
