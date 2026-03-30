import type { AuthState } from '../../shared/types';

interface AuthDialogProps {
  open: boolean;
  authState: AuthState;
  onClose: () => void;
  onInit: () => void;
  onGenerateUrl: () => void;
  onToggleListener: () => void;
  onRestartListener: () => void;
  onCapture: () => void;
}

export function AuthDialog({
  open,
  authState,
  onClose,
  onInit,
  onGenerateUrl,
  onToggleListener,
  onRestartListener,
  onCapture,
}: AuthDialogProps) {
  if (!open) return null;

  return (
    <dialog open id="authDialog" onClick={(event) => event.currentTarget === event.target && onClose()}>
      <header className="dialog-header">
        <h3>OAuth setup</h3>
        <button className="dialog-close" type="button" aria-label="Close" onClick={onClose}>
          &times;
        </button>
      </header>
      <div className="dialog-body">
        <div className="summary-grid">
          <div>
            <span>Setup</span>
            <strong>{authState.initStatus}</strong>
          </div>
          <div>
            <span>Listener</span>
            <strong>{authState.listenerRunning ? 'running' : 'stopped'}</strong>
          </div>
          <div>
            <span>Callback</span>
            <strong className="mono">{authState.listenerUrl}</strong>
          </div>
          <div>
            <span>Last callback</span>
            <strong>{authState.lastResponse}</strong>
          </div>
        </div>
        <div className="button-row">
          <button className="dock-btn" type="button" onClick={onInit}>
            Initialize
          </button>
          <button className="dock-btn" type="button" onClick={onGenerateUrl}>
            Generate URL
          </button>
          <button className="dock-btn" type="button" onClick={onToggleListener}>
            {authState.listenerRunning ? 'Stop Listener' : 'Start Listener'}
          </button>
          <button className="dock-btn" type="button" onClick={onRestartListener}>
            Restart Listener
          </button>
          <button className="dock-btn accent" type="button" onClick={onCapture}>
            Simulate Callback
          </button>
        </div>
        <div className="footnote">
          OAuth callback server control is tied to <span className="mono">/api/oauth/start</span>,{' '}
          <span className="mono">/api/oauth/stop</span>, <span className="mono">/api/oauth/restart</span>,{' '}
          <span className="mono">/api/oauth/status</span>, and <span className="mono">/auth/callback</span>.
        </div>
      </div>
    </dialog>
  );
}
