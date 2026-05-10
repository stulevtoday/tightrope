import { useTranslation } from 'react-i18next';

interface AccountImportDialogActionsProps {
  stage: 'idle' | 'scanning' | 'ready' | 'importing' | 'done' | 'error';
  importEnabled: boolean;
  rescanEnabled: boolean;
  onCancel: () => void;
  onRescan: () => void;
  onImport: () => void;
}

export function AccountImportDialogActions({
  stage,
  importEnabled,
  rescanEnabled,
  onCancel,
  onRescan,
  onImport,
}: AccountImportDialogActionsProps) {
  const { t } = useTranslation();
  if (stage === 'done') {
    return (
      <div className="dialog-actions">
        <button className="dock-btn" type="button" onClick={onCancel}>
          {t('dialogs.add_account_close')}
        </button>
      </div>
    );
  }

  return (
    <div className="dialog-actions">
      <button className="dock-btn" type="button" onClick={onCancel}>
        {t('dialogs.add_account_cancel')}
      </button>
      <button className="dock-btn" type="button" disabled={!rescanEnabled} onClick={onRescan}>
        {t('dialogs.account_import_rescan')}
      </button>
      <button className="dock-btn accent" type="button" disabled={!importEnabled} onClick={onImport}>
        {stage === 'importing' ? t('dialogs.account_import_importing') : t('dialogs.account_import_import_delta')}
      </button>
    </div>
  );
}
