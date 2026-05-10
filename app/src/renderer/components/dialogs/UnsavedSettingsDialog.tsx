import { useTranslation } from 'react-i18next';
import { useNavigationContext } from '../../state/context';

export function UnsavedSettingsDialog() {
  const { t } = useTranslation();
  const navigation = useNavigationContext();
  const open = navigation.settingsLeaveDialogOpen;
  const busy = navigation.settingsSaveInFlight;

  if (!open) {
    return null;
  }

  return (
    <dialog
      open
      id="unsavedSettingsDialog"
      onClick={(event) => {
        if (event.currentTarget === event.target && !busy) {
          navigation.closeSettingsLeaveDialog();
        }
      }}
    >
      <header className="dialog-header">
        <h3>{t('dialogs.unsaved_title')}</h3>
      </header>
      <div className="dialog-body">
        <p className="unsaved-settings-copy">
          {t('dialogs.unsaved_message')}
        </p>
        <div className="dialog-actions">
          <button className="dock-btn" type="button" disabled={busy} onClick={navigation.closeSettingsLeaveDialog}>
            {t('dialogs.unsaved_cancel')}
          </button>
          <button className="dock-btn" type="button" disabled={busy} onClick={navigation.discardSettingsAndNavigate}>
            {t('dialogs.unsaved_discard')}
          </button>
          <button className="dock-btn accent" type="button" disabled={busy} onClick={() => void navigation.saveSettingsAndNavigate()}>
            {busy ? t('dialogs.unsaved_saving') : t('dialogs.unsaved_save')}
          </button>
        </div>
      </div>
    </dialog>
  );
}
