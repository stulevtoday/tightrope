import { useEffect, useState } from 'react';
import { getCurrentStatusNotice, subscribeStatusNotice } from '../../state/statusNotices';

export function StatusBar() {
  const [notice, setNotice] = useState(getCurrentStatusNotice);

  useEffect(() => subscribeStatusNotice(setNotice), []);

  return (
    <footer className="statusbar" role="status" aria-live={notice.level === 'error' ? 'assertive' : 'polite'}>
      <span className={`status-dot ${notice.level}`} aria-hidden="true" />
      <div className="status-well">
        <span className="status-label">Status</span>
        <span key={notice.sequence} className="status-message">
          {notice.message}
        </span>
      </div>
      <span className="status-time">{notice.at}</span>
    </footer>
  );
}
