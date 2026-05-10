import { useTranslation } from 'react-i18next';

interface AccountImportSectionProps {
  importWithoutOverwrite: boolean;
  onSetImportWithoutOverwrite: (enabled: boolean) => void;
  onOpenImportDialog: () => void;
}

export function AccountImportSection({
  importWithoutOverwrite,
  onSetImportWithoutOverwrite,
  onOpenImportDialog,
}: AccountImportSectionProps) {
  const { t } = useTranslation();
  return (
    <div className="settings-group">
      <div className="settings-group-header">
        <h3>{t('settings.account_import_title')}</h3>
        <p>{t('settings.account_import_desc')}</p>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.import_source_db')}</strong>
          <span>{t('settings.import_source_db_desc')}</span>
        </div>
        <button className="dock-btn accent" type="button" onClick={onOpenImportDialog}>
          {t('settings.import_sqlite_db')}
        </button>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.import_without_overwrite')}</strong>
          <span>{t('settings.import_without_overwrite_desc')}</span>
        </div>
        <button
          className={`setting-toggle${importWithoutOverwrite ? ' on' : ''}`}
          type="button"
          aria-label={t('settings.import_without_overwrite_aria')}
          onClick={() => onSetImportWithoutOverwrite(!importWithoutOverwrite)}
        />
      </div>
    </div>
  );
}
