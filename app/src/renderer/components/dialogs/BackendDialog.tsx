import type { RuntimeState } from '../../shared/types';
import { currentRouterState } from '../../state/logic';

interface BackendDialogProps {
  open: boolean;
  runtimeState: RuntimeState;
  onClose: () => void;
  onStart: () => void;
  onRestart: () => void;
  onStop: () => void;
  onToggleAutoRestart: () => void;
}

export function BackendDialog({ open, runtimeState, onClose, onStart, onRestart, onStop, onToggleAutoRestart }: BackendDialogProps) {
  if (!open) return null;
  const routerState = currentRouterState(runtimeState);

  return (
    <dialog open id="backendDialog" onClick={(event) => event.currentTarget === event.target && onClose()}>
      <header className="dialog-header">
        <h3>Backend</h3>
        <button className="dialog-close" type="button" aria-label="Close" onClick={onClose}>
          &times;
        </button>
      </header>
      <div className="dialog-body">
        <div className="summary-grid">
          <div>
            <span>Router</span>
            <strong>{routerState}</strong>
          </div>
          <div>
            <span>Backend</span>
            <strong>{runtimeState.backend}</strong>
          </div>
          <div>
            <span>Health</span>
            <strong>{runtimeState.health}</strong>
          </div>
          <div>
            <span>Restart</span>
            <strong>{runtimeState.autoRestart ? 'armed' : 'manual'}</strong>
          </div>
        </div>
        <div className="button-row">
          <button className="dock-btn" type="button" onClick={onStart}>
            Start
          </button>
          <button className="dock-btn" type="button" onClick={onRestart}>
            Restart
          </button>
          <button className="dock-btn" type="button" onClick={onStop}>
            Stop
          </button>
          <button className="dock-btn accent" type="button" onClick={onToggleAutoRestart}>
            {runtimeState.autoRestart ? 'Disable Auto-Restart' : 'Enable Auto-Restart'}
          </button>
        </div>
        <div className="footnote">
          <span className="mono">{runtimeState.bind}</span> loopback • {runtimeState.pausedRoutes ? 'traffic paused' : 'accepting traffic'}
        </div>
      </div>
    </dialog>
  );
}
