import type { RouterRuntimeState } from '../../shared/types';

interface RouterToolbarProps {
  visible: boolean;
  routerState: RouterRuntimeState;
  strategyLabel: string;
  activeAccountsLabel: string;
  bindLabel: string;
  searchQuery: string;
  pauseLabel: string;
  onSearch: (value: string) => void;
  onStart: () => void;
  onRestart: () => void;
  onPause: () => void;
  onOpenBackend: () => void;
  onOpenAuth: () => void;
}

export function RouterToolbar({
  visible,
  routerState,
  strategyLabel,
  activeAccountsLabel,
  bindLabel,
  searchQuery,
  pauseLabel,
  onSearch,
  onStart,
  onRestart,
  onPause,
  onOpenBackend,
  onOpenAuth,
}: RouterToolbarProps) {
  if (!visible) return null;

  return (
    <header className="tool-strip">
      <div className="tool-context">
        <div className="router-state" data-state={routerState}>
          <span className="state-dot" aria-hidden="true" />
          <div className="router-state-copy">
            <strong>Router {routerState}</strong>
            <span>
              {routerState === 'stopped'
                ? 'Waiting for backend start'
                : routerState === 'paused'
                  ? 'New traffic paused, sessions visible'
                  : routerState === 'degraded'
                    ? 'Health checks need attention'
                    : 'Child process managed by Electron'}
            </span>
          </div>
        </div>
        <div className="tool-chip">
          <span>Bind</span>
          <strong className="mono">{bindLabel}</strong>
        </div>
        <div className="tool-chip">
          <span>Strategy</span>
          <strong>{strategyLabel}</strong>
        </div>
        <div className="tool-chip">
          <span>Eligible</span>
          <strong>{activeAccountsLabel}</strong>
        </div>
      </div>
      <div className="tool-actions">
        <input
          className="search"
          type="search"
          value={searchQuery}
          placeholder="Search by ID, model, or account"
          aria-label="Search routed requests"
          onChange={(event) => onSearch(event.target.value)}
        />
        <button className="tool-btn" id="startRouter" type="button" onClick={onStart}>
          Start
        </button>
        <button className="tool-btn" id="restartRouter" type="button" onClick={onRestart}>
          Restart
        </button>
        <button className="tool-btn accent" id="pauseRouter" type="button" onClick={onPause}>
          {pauseLabel}
        </button>
        <span className="tool-sep" aria-hidden="true" />
        <button className="tool-btn" id="openBackendDialog" type="button" onClick={onOpenBackend}>
          Backend
        </button>
        <button className="tool-btn" id="openAuthDialog" type="button" onClick={onOpenAuth}>
          OAuth
        </button>
      </div>
    </header>
  );
}
