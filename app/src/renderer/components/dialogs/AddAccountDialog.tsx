import { CheckCircle2 } from 'lucide-react';
import type { AddAccountStep } from '../../shared/types';

interface AddAccountDialogProps {
  open: boolean;
  step: AddAccountStep;
  selectedFileName: string;
  manualCallback: string;
  browserAuthUrl: string;
  deviceVerifyUrl: string;
  deviceUserCode: string;
  deviceCountdownSeconds: number;
  copyAuthLabel: 'Copy' | 'Copied';
  copyDeviceLabel: 'Copy' | 'Copied';
  successEmail: string;
  successPlan: string;
  errorMessage: string;
  onClose: () => void;
  onSetStep: (step: AddAccountStep) => void;
  onSelectFile: (file: File) => void;
  onSubmitImport: () => void;
  onStartBrowserFlow: () => void;
  onSubmitManualCallback: () => void;
  onSetManualCallback: (value: string) => void;
  onCopyAuthUrl: () => void;
  onStartDeviceFlow: () => void;
  onCancelDeviceFlow: () => void;
  onCopyDeviceUrl: () => void;
  onOpenDeviceUrl: () => void;
  onDoneSuccess: () => void;
}

function titleForStep(step: AddAccountStep): string {
  if (step === 'stepImport') return 'Import auth.json';
  if (step === 'stepBrowser') return 'Browser sign-in';
  if (step === 'stepDevice') return 'Device code';
  return 'Add account';
}

function countdownLabel(seconds: number): string {
  const mins = Math.floor(seconds / 60);
  const secs = seconds % 60;
  return `Expires in ${mins}:${String(secs).padStart(2, '0')}`;
}

export function AddAccountDialog({
  open,
  step,
  selectedFileName,
  manualCallback,
  browserAuthUrl,
  deviceVerifyUrl,
  deviceUserCode,
  deviceCountdownSeconds,
  copyAuthLabel,
  copyDeviceLabel,
  successEmail,
  successPlan,
  errorMessage,
  onClose,
  onSetStep,
  onSelectFile,
  onSubmitImport,
  onStartBrowserFlow,
  onSubmitManualCallback,
  onSetManualCallback,
  onCopyAuthUrl,
  onStartDeviceFlow,
  onCancelDeviceFlow,
  onCopyDeviceUrl,
  onOpenDeviceUrl,
  onDoneSuccess,
}: AddAccountDialogProps) {
  if (!open) return null;
  const hasBrowserAuthUrl = browserAuthUrl.trim().length > 0;

  return (
    <dialog open id="addAccountDialog" onClick={(event) => event.currentTarget === event.target && onClose()}>
      <header className="dialog-header">
        <h3>{titleForStep(step)}</h3>
        <button className="dialog-close" type="button" aria-label="Close" onClick={onClose}>
          &times;
        </button>
      </header>
      <div className="dialog-body">
        {step === 'stepMethod' && (
          <div className="step active">
            <div className="method-list">
              <button className="method-option" type="button" onClick={() => onSetStep('stepBrowser')}>
                <strong>
                  Browser sign-in <span className="method-tag">recommended</span>
                </strong>
                <span>Opens a browser window for OpenAI authentication via PKCE</span>
              </button>
              <button
                className="method-option"
                type="button"
                onClick={() => {
                  onSetStep('stepDevice');
                  onStartDeviceFlow();
                }}
              >
                <strong>Device code</strong>
                <span>Enter a code on another device. For headless or remote setups</span>
              </button>
              <button className="method-option" type="button" onClick={() => onSetStep('stepImport')}>
                <strong>Import auth.json</strong>
                <span>Upload an existing auth.json file with pre-exported credentials</span>
              </button>
            </div>
          </div>
        )}

        {step === 'stepImport' && (
          <div className="step active">
            <label className="file-drop" htmlFor="fileInput">
              <p>Drop auth.json here or click to browse</p>
              <small>JSON file with tokens, lastRefreshAt, and optional OPENAI_API_KEY</small>
              <input
                id="fileInput"
                type="file"
                accept=".json,application/json"
                onChange={(event) => {
                  const file = event.target.files?.[0];
                  if (file) onSelectFile(file);
                }}
              />
              <div className="file-name">{selectedFileName}</div>
            </label>
            <div className="dialog-actions">
              <button className="dock-btn" type="button" onClick={() => onSetStep('stepMethod')}>
                Back
              </button>
              <button className="dock-btn accent" type="button" disabled={!selectedFileName} onClick={onSubmitImport}>
                Import
              </button>
            </div>
          </div>
        )}

        {step === 'stepBrowser' && (
          <div className="step active">
            <div className="pending-state">
              <p className="pending-status">Waiting for authorization…</p>
              <div className="url-row">
                <span className="url-value">{hasBrowserAuthUrl ? browserAuthUrl : 'Preparing authorization URL…'}</span>
                <button className="copy-btn" type="button" disabled={!hasBrowserAuthUrl} onClick={onCopyAuthUrl}>
                  {copyAuthLabel}
                </button>
              </div>
              <div className="button-row">
                <button className="dock-btn accent" type="button" onClick={onStartBrowserFlow}>
                  Open sign-in page
                </button>
              </div>
              <div className="manual-section">
                <label>Remote server? Paste the callback URL after sign-in:</label>
                <div className="url-row">
                  <input
                    className="auth-input"
                    type="text"
                    placeholder="http://127.0.0.1:1455/auth/callback?code=..."
                    value={manualCallback}
                    onChange={(event) => onSetManualCallback(event.target.value)}
                  />
                  <button className="dock-btn" type="button" onClick={onSubmitManualCallback}>
                    Submit
                  </button>
                </div>
              </div>
            </div>
            <div className="dialog-actions">
              <button className="dock-btn" type="button" onClick={() => onSetStep('stepMethod')}>
                Cancel
              </button>
            </div>
          </div>
        )}

        {step === 'stepDevice' && (
          <div className="step active">
            <div className="pending-state">
              <p className="pending-status">Enter this code at the verification page:</p>
              <div className="code-display">{deviceUserCode}</div>
              <div className="url-row">
                <span className="url-value">{deviceVerifyUrl}</span>
                <button className="copy-btn" type="button" onClick={onCopyDeviceUrl}>
                  {copyDeviceLabel}
                </button>
              </div>
              <div className="button-row">
                <button className="dock-btn accent" type="button" onClick={onOpenDeviceUrl}>
                  Open verification page
                </button>
              </div>
              <p className="countdown">{countdownLabel(deviceCountdownSeconds)}</p>
            </div>
            <div className="dialog-actions">
              <button className="dock-btn" type="button" onClick={onCancelDeviceFlow}>
                Cancel
              </button>
            </div>
          </div>
        )}

        {step === 'stepSuccess' && (
          <div className="step active">
            <div className="success-state">
              <div className="success-check">
                <CheckCircle2 size={20} strokeWidth={2.25} aria-hidden="true" />
              </div>
              <p>Account added</p>
              <small>{successEmail}</small>
              <small>{successPlan}</small>
            </div>
            <div className="dialog-actions">
              <button className="dock-btn accent" type="button" onClick={onDoneSuccess}>
                Done
              </button>
            </div>
          </div>
        )}

        {step === 'stepError' && (
          <div className="step active">
            <div className="error-state">
              <p>{errorMessage}</p>
            </div>
            <div className="dialog-actions">
              <button className="dock-btn" type="button" onClick={() => onSetStep('stepMethod')}>
                Try again
              </button>
              <button className="dock-btn" type="button" onClick={onClose}>
                Close
              </button>
            </div>
          </div>
        )}
      </div>
    </dialog>
  );
}
