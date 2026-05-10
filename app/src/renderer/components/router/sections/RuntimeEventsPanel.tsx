import { useTranslation } from 'react-i18next';
import type { RuntimeState } from '../../../shared/types';

interface RuntimeEventsPanelProps {
  runtimeState: RuntimeState;
}

export function RuntimeEventsPanel({ runtimeState }: RuntimeEventsPanelProps) {
  const { t } = useTranslation();
  return (
    <section className="events-panel">
      <header className="events-header">
        <div>
          <p className="eyebrow">{t('router.events_eyebrow')}</p>
          <h3>{t('router.events_title')}</h3>
        </div>
      </header>
      <div className="events-body">
        {runtimeState.events.slice(0, 10).map((event, index) => (
          <div key={`${index}-${event}`} className="log-line">
            <span className="log-ts">{event.slice(0, 8)}</span>
            <span className="log-msg">{event.slice(9)}</span>
          </div>
        ))}
      </div>
    </section>
  );
}
