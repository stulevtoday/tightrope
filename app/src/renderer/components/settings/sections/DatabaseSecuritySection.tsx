import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useTightropeService } from '../../../state/context';

export function DatabaseSecuritySection() {
  const { t } = useTranslation();
  const service = useTightropeService();
  const [currentPassphrase, setCurrentPassphrase] = useState('');
  const [nextPassphrase, setNextPassphrase] = useState('');
  const [confirmation, setConfirmation] = useState('');
  const [saving, setSaving] = useState(false);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [successMessage, setSuccessMessage] = useState<string | null>(null);

  async function submitChange(): Promise<void> {
    setErrorMessage(null);
    setSuccessMessage(null);

    if (currentPassphrase.length === 0) {
      setErrorMessage(t('settings.db_current_passphrase_required'));
      return;
    }
    if (nextPassphrase.length < 8) {
      setErrorMessage(t('settings.db_new_passphrase_min'));
      return;
    }
    if (nextPassphrase !== confirmation) {
      setErrorMessage(t('settings.db_passphrase_mismatch'));
      return;
    }
    if (nextPassphrase === currentPassphrase) {
      setErrorMessage(t('settings.db_passphrase_same'));
      return;
    }

    setSaving(true);
    try {
      await service.changeDatabasePassphraseRequest(currentPassphrase, nextPassphrase);
      setSuccessMessage(t('settings.db_passphrase_changed'));
      setCurrentPassphrase('');
      setNextPassphrase('');
      setConfirmation('');
    } catch (error) {
      const message = error instanceof Error ? error.message : t('settings.db_passphrase_change_failed');
      setErrorMessage(message);
    } finally {
      setSaving(false);
    }
  }

  return (
    <div className="settings-group">
      <div className="settings-group-header">
        <h3>{t('settings.db_security_title')}</h3>
        <p>{t('settings.db_security_desc')}</p>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.db_change_password')}</strong>
          <span>{t('settings.db_change_password_desc')}</span>
        </div>
        <div className="setting-inline-fields">
          <input
            className="setting-select setting-input--password"
            type="password"
            placeholder={t('settings.db_placeholder_current')}
            value={currentPassphrase}
            onChange={(event) => setCurrentPassphrase(event.target.value)}
            autoComplete="current-password"
            disabled={saving}
          />
          <input
            className="setting-select setting-input--password"
            type="password"
            placeholder={t('settings.db_placeholder_new')}
            value={nextPassphrase}
            onChange={(event) => setNextPassphrase(event.target.value)}
            autoComplete="new-password"
            disabled={saving}
          />
          <input
            className="setting-select setting-input--password"
            type="password"
            placeholder={t('settings.db_placeholder_confirm')}
            value={confirmation}
            onChange={(event) => setConfirmation(event.target.value)}
            autoComplete="new-password"
            disabled={saving}
          />
          <button className="dock-btn accent" type="button" onClick={() => void submitChange()} disabled={saving}>
            {saving ? t('settings.db_updating') : t('settings.db_change_password_button')}
          </button>
        </div>
      </div>
      {(errorMessage || successMessage) && (
        <div className={`setting-feedback${errorMessage ? ' error' : ' success'}`}>
          {errorMessage ?? successMessage}
        </div>
      )}
    </div>
  );
}
