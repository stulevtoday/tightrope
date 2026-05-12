import { CheckCircle2 } from 'lucide-react';
import { useTranslation } from 'react-i18next';
import type { AddAccountStep } from '../../shared/types';
import { useAccountsContext } from '../../state/context';

function titleForStep(step: AddAccountStep, t: (key: string) => string): string {
  if (step === 'stepImport') return t('dialogs.add_account_import_json');
  if (step === 'stepBrowser') return t('dialogs.add_account_browser_sign_in');
  if (step === 'stepDevice') return t('dialogs.add_account_device_code');
  return t('dialogs.add_account_title');
}

function countdownLabel(seconds: number, t: (key: string, options?: Record<string, unknown>) => string): string {
  const mins = Math.floor(seconds / 60);
  const secs = String(seconds % 60).padStart(2, '0');
  return t('dialogs.add_account_expires_in', { min: mins, sec: secs });
}

export function AddAccountDialog() {
  const { t } = useTranslation();
  const accounts = useAccountsContext();
  if (!accounts.addAccountOpen) return null;

  const step = accounts.addAccountStep;
  const hasBrowserAuthUrl = accounts.browserAuthUrl.trim().length > 0;

  return (
    <dialog open id="addAccountDialog" onClick={(event) => event.currentTarget === event.target && accounts.closeAddAccountDialog()}>
      <header className="dialog-header">
        <h3>{titleForStep(step, t)}</h3>
        <button className="dialog-close" type="button" aria-label={t('common.close')} onClick={accounts.closeAddAccountDialog}>
          &times;
        </button>
      </header>
      <div className="dialog-body">
        {step === 'stepMethod' && (
          <div className="step active">
            <div className="method-list">
              <button className="method-option" type="button" onClick={() => accounts.setAddAccountStep('stepBrowser')}>
                <strong>
                  {t('dialogs.add_account_browser_sign_in')} <span className="method-tag">{t('dialogs.add_account_browser_recommended')}</span>
                </strong>
                <span>{t('dialogs.add_account_browser_desc')}</span>
              </button>
              <button
                className="method-option"
                type="button"
                onClick={() => {
                  accounts.setAddAccountStep('stepDevice');
                  accounts.startDeviceFlow();
                }}
              >
                <strong>{t('dialogs.add_account_device_code')}</strong>
                <span>{t('dialogs.add_account_device_desc')}</span>
              </button>
              <button className="method-option" type="button" onClick={() => accounts.setAddAccountStep('stepImport')}>
                <strong>{t('dialogs.add_account_import_json')}</strong>
                <span>{t('dialogs.add_account_import_desc')}</span>
              </button>
            </div>
          </div>
        )}

        {step === 'stepImport' && (
          <div className="step active">
            <label className="file-drop" htmlFor="fileInput">
              <p>{t('dialogs.add_account_drop_json')}</p>
              <small>{t('dialogs.add_account_drop_json_hint')}</small>
              <input
                id="fileInput"
                type="file"
                accept=".json,application/json"
                onChange={(event) => {
                  const file = event.target.files?.[0];
                  if (file) accounts.selectImportFile(file);
                }}
              />
              <div className="file-name">{accounts.selectedFileName}</div>
            </label>
            <div className="dialog-actions">
              <button className="dock-btn" type="button" onClick={() => accounts.setAddAccountStep('stepMethod')}>
                {t('dialogs.add_account_back')}
              </button>
              <button className="dock-btn accent" type="button" disabled={!accounts.selectedFileName} onClick={accounts.submitImport}>
                {t('dialogs.add_account_import_button')}
              </button>
            </div>
          </div>
        )}

        {step === 'stepBrowser' && (
          <div className="step active">
            <div className="pending-state">
              <p className="pending-status">{t('dialogs.add_account_waiting_auth')}</p>
              <div className="url-row">
                <span className="url-value">{hasBrowserAuthUrl ? accounts.browserAuthUrl : t('dialogs.add_account_preparing_url')}</span>
                <button className="copy-btn" type="button" disabled={!hasBrowserAuthUrl} onClick={() => void accounts.copyBrowserAuthUrl()}>
                  {accounts.copyAuthLabel}
                </button>
              </div>
              <div className="button-row">
                <button className="dock-btn accent" type="button" onClick={accounts.simulateBrowserAuth}>
                  {t('dialogs.add_account_open_sign_in')}
                </button>
              </div>
              <div className="manual-section">
                <label>{t('dialogs.add_account_remote_server_label')}</label>
                <div className="url-row">
                  <input
                    className="auth-input"
                    type="text"
                    placeholder={t('dialogs.add_account_callback_placeholder')}
                    value={accounts.manualCallback}
                    onChange={(event) => accounts.setManualCallback(event.target.value)}
                  />
                  <button className="dock-btn" type="button" onClick={accounts.submitManualCallback}>
                    {t('dialogs.add_account_submit')}
                  </button>
                </div>
              </div>
            </div>
            <div className="dialog-actions">
              <button className="dock-btn" type="button" onClick={() => accounts.setAddAccountStep('stepMethod')}>
                {t('dialogs.add_account_cancel')}
              </button>
            </div>
          </div>
        )}

        {step === 'stepDevice' && (
          <div className="step active">
            <div className="pending-state">
              <p className="pending-status">{t('dialogs.add_account_device_enter_code')}</p>
              <div className="code-display">{accounts.deviceUserCode}</div>
              <div className="url-row">
                <span className="url-value">{accounts.deviceVerifyUrl}</span>
                <button className="copy-btn" type="button" onClick={() => void accounts.copyDeviceVerificationUrl()}>
                  {accounts.copyDeviceLabel}
                </button>
              </div>
              <div className="button-row">
                <button
                  className="dock-btn accent"
                  type="button"
                  onClick={() => window.open(accounts.deviceVerifyUrl, '_blank', 'noopener,noreferrer')}
                >
                  {t('dialogs.add_account_open_verification')}
                </button>
              </div>
              <p className="countdown">{countdownLabel(accounts.deviceCountdownSeconds, t)}</p>
            </div>
            <div className="dialog-actions">
              <button className="dock-btn" type="button" onClick={accounts.cancelDeviceFlow}>
                {t('dialogs.add_account_cancel')}
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
              <p>{t('dialogs.add_account_account_added')}</p>
              <small>{accounts.successEmail}</small>
              <small>{accounts.successPlan}</small>
            </div>
            <div className="dialog-actions">
              <button className="dock-btn accent" type="button" onClick={accounts.closeAddAccountDialog}>
                {t('dialogs.add_account_done')}
              </button>
            </div>
          </div>
        )}

        {step === 'stepError' && (
          <div className="step active">
            <div className="error-state">
              <p>{accounts.addAccountError}</p>
            </div>
            <div className="dialog-actions">
              <button className="dock-btn" type="button" onClick={() => accounts.setAddAccountStep('stepMethod')}>
                {t('dialogs.add_account_try_again')}
              </button>
              <button className="dock-btn" type="button" onClick={accounts.closeAddAccountDialog}>
                {t('dialogs.add_account_close')}
              </button>
            </div>
          </div>
        )}
      </div>
    </dialog>
  );
}
