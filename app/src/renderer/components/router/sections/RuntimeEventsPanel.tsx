import type { RuntimeState } from '../../../shared/types';

interface RuntimeEventsPanelProps {
  runtimeState: RuntimeState;
}

export function RuntimeEventsPanel({ runtimeState }: RuntimeEventsPanelProps) {
  return (
    <section className="events-panel">
      <header className="events-header">
        <div>
          <p className="eyebrow">Log</p>
          <h3>Runtime events</h3>
        </div>
      </header>
      <div className="events-body">
        {runtimeState.events.slice(0, 10).map((event) => (
          <div key={event} className="log-line">
            <span className="log-ts">{event.slice(0, 8)}</span>
            <span className="log-msg">{event.slice(9)}</span>
          </div>
        ))}
      </div>
    </section>
  );
}
