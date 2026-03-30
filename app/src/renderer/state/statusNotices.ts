import { nowStamp } from './logic';

export type StatusNoticeLevel = 'info' | 'success' | 'warn' | 'error';

export interface StatusNotice {
  sequence: number;
  message: string;
  level: StatusNoticeLevel;
  at: string;
}

export interface PublishStatusNoticeInput {
  message: string;
  level?: StatusNoticeLevel;
}

type StatusNoticeListener = (notice: StatusNotice) => void;

const listeners = new Set<StatusNoticeListener>();

let sequenceCounter = 1;
let currentStatusNotice: StatusNotice = {
  sequence: 0,
  message: 'Ready',
  level: 'info',
  at: nowStamp(),
};

export function getCurrentStatusNotice(): StatusNotice {
  return currentStatusNotice;
}

export function subscribeStatusNotice(listener: StatusNoticeListener): () => void {
  listeners.add(listener);
  listener(currentStatusNotice);
  return () => {
    listeners.delete(listener);
  };
}

export function publishStatusNotice(input: PublishStatusNoticeInput | string): StatusNotice {
  const payload: PublishStatusNoticeInput = typeof input === 'string' ? { message: input } : input;
  currentStatusNotice = {
    sequence: sequenceCounter++,
    message: payload.message,
    level: payload.level ?? 'info',
    at: nowStamp(),
  };

  listeners.forEach((listener) => listener(currentStatusNotice));
  return currentStatusNotice;
}

export function resetStatusNoticesForTests(): void {
  sequenceCounter = 1;
  currentStatusNotice = {
    sequence: 0,
    message: 'Ready',
    level: 'info',
    at: nowStamp(),
  };
  listeners.clear();
}
