import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useAccountsContext } from '../../state/context';
import { getCurrentStatusNotice, subscribeStatusNotice } from '../../state/statusNotices';

interface TrafficTotals {
  upBytes: number;
  downBytes: number;
}

function formatClockTime(timestampMs: number): string {
  const date = new Date(timestampMs);
  const hours = date.getHours().toString().padStart(2, '0');
  const minutes = date.getMinutes().toString().padStart(2, '0');
  const seconds = date.getSeconds().toString().padStart(2, '0');
  return `${hours}:${minutes}:${seconds}`;
}

function clampNonNegative(value: number | null | undefined): number {
  if (!Number.isFinite(value)) {
    return 0;
  }
  return Math.max(0, value ?? 0);
}

function formatBytes(value: number): string {
  const units = ['B', 'KB', 'MB', 'GB', 'TB', 'PB'];
  const bytes = clampNonNegative(value);
  if (bytes < 1024) {
    return `${Math.round(bytes)} B`;
  }
  let scaled = bytes;
  let unitIndex = 0;
  while (scaled >= 1024 && unitIndex < units.length - 1) {
    scaled /= 1024;
    unitIndex += 1;
  }
  const precision = scaled >= 100 ? 0 : scaled >= 10 ? 1 : 2;
  return `${scaled.toFixed(precision)} ${units[unitIndex]}`;
}

export function StatusBar() {
  const { t } = useTranslation();
  const accounts = useAccountsContext();
  const [notice, setNotice] = useState(getCurrentStatusNotice);
  const [sessionTraffic, setSessionTraffic] = useState<TrafficTotals>({ upBytes: 0, downBytes: 0 });
  const [clockLabel, setClockLabel] = useState(() => formatClockTime(Date.now()));
  const [sendPulseVersion, setSendPulseVersion] = useState(0);
  const [receivePulseVersion, setReceivePulseVersion] = useState(0);
  const [sendActive, setSendActive] = useState(false);
  const [receiveActive, setReceiveActive] = useState(false);
  const lastTrafficByAccountRef = useRef<Map<string, TrafficTotals>>(new Map());
  const sendActivityTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const receiveActivityTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => subscribeStatusNotice(setNotice), []);
  useEffect(() => {
    const handle = setInterval(() => {
      setClockLabel(formatClockTime(Date.now()));
    }, 1000);
    return () => {
      clearInterval(handle);
    };
  }, []);

  useEffect(
    () => () => {
      if (sendActivityTimeoutRef.current !== null) {
        clearTimeout(sendActivityTimeoutRef.current);
      }
      if (receiveActivityTimeoutRef.current !== null) {
        clearTimeout(receiveActivityTimeoutRef.current);
      }
    },
    [],
  );

  useEffect(() => {
    let upDelta = 0;
    let downDelta = 0;
    const counters = lastTrafficByAccountRef.current;
    for (const account of accounts.accounts) {
      const nextUp = clampNonNegative(account.trafficUpBytes);
      const nextDown = clampNonNegative(account.trafficDownBytes);
      const previous = counters.get(account.id) ?? { upBytes: 0, downBytes: 0 };
      upDelta += nextUp >= previous.upBytes ? nextUp - previous.upBytes : nextUp;
      downDelta += nextDown >= previous.downBytes ? nextDown - previous.downBytes : nextDown;
      counters.set(account.id, { upBytes: nextUp, downBytes: nextDown });
    }
    if (upDelta <= 0 && downDelta <= 0) {
      return;
    }
    if (upDelta > 0) {
      setSendPulseVersion((value) => value + 1);
      setSendActive(true);
      if (sendActivityTimeoutRef.current !== null) {
        clearTimeout(sendActivityTimeoutRef.current);
      }
      sendActivityTimeoutRef.current = setTimeout(() => {
        setSendActive(false);
      }, 360);
    }
    if (downDelta > 0) {
      setReceivePulseVersion((value) => value + 1);
      setReceiveActive(true);
      if (receiveActivityTimeoutRef.current !== null) {
        clearTimeout(receiveActivityTimeoutRef.current);
      }
      receiveActivityTimeoutRef.current = setTimeout(() => {
        setReceiveActive(false);
      }, 360);
    }
    setSessionTraffic((current) => ({
      upBytes: current.upBytes + upDelta,
      downBytes: current.downBytes + downDelta,
    }));
  }, [accounts.accounts]);

  const txText = formatBytes(sessionTraffic.upBytes);
  const rxText = formatBytes(sessionTraffic.downBytes);
  const progress = notice.progress;
  const progressPercent =
    progress && progress.total > 0
      ? Math.max(0, Math.min(100, Math.round((progress.current / progress.total) * 100)))
      : 0;
  const progressText = progress ? `${progress.current}/${progress.total}` : '';

  return (
    <footer className="statusbar" role="status" aria-live={notice.level === 'error' ? 'assertive' : 'polite'}>
      <span className={`status-dot ${notice.level}`} aria-hidden="true" />
      <div className="status-well">
        <span className="status-label">{t('status.label')}</span>
        {notice.renderMode === 'progress' && progress ? (
          <div
            className="status-progress"
            role="progressbar"
            aria-label={progress.label}
            aria-valuemin={0}
            aria-valuemax={progress.total}
            aria-valuenow={progress.current}
          >
            <span className="status-progress-fill" style={{ width: `${progressPercent}%` }} />
            <span className="status-progress-text">{progressText}</span>
          </div>
        ) : (
          <span key={notice.sequence} className="status-message">
            {notice.message}
          </span>
        )}
      </div>
      <span className="status-traffic" aria-label={t('status.sent_received', { tx: txText, rx: rxText })}>
        <span
          key={`send-dot-${sendPulseVersion}`}
          className={`status-traffic-dot send${sendActive ? ' active' : ''}`}
          aria-hidden="true"
        />
        <span key={`tx-${sessionTraffic.upBytes}`} className="status-traffic-value">
          {txText}
        </span>
        <span className="status-traffic-separator" aria-hidden="true">
          ·
        </span>
        <span
          key={`receive-dot-${receivePulseVersion}`}
          className={`status-traffic-dot receive${receiveActive ? ' active' : ''}`}
          aria-hidden="true"
        />
        <span key={`rx-${sessionTraffic.downBytes}`} className="status-traffic-value">
          {rxText}
        </span>
      </span>
      <span className="status-time">{clockLabel}</span>
    </footer>
  );
}
