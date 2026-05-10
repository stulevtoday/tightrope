import { useTranslation } from 'react-i18next';

interface AccountImportSourceDatabasePassphrasePromptProps {
  value: string;
  required: boolean;
  disabled: boolean;
  onChange: (value: string) => void;
}

export function AccountImportSourceDatabasePassphrasePrompt({
  value,
  required,
  disabled,
  onChange,
}: AccountImportSourceDatabasePassphrasePromptProps) {
  const { t } = useTranslation();
  return (
    <section className="account-import-source-key">
      <div className="account-import-source-key-header">
        <strong>{t('dialogs.account_import_source_db_pass_title')}</strong>
        {required && <span>{t('dialogs.account_import_source_db_pass_required')}</span>}
      </div>
      <input
        className="dock-input account-import-source-key-input"
        type="password"
        spellCheck={false}
        autoComplete="off"
        placeholder={t('dialogs.account_import_source_db_pass_placeholder')}
        value={value}
        disabled={disabled}
        onChange={(event) => onChange(event.target.value)}
      />
      <small>{t('dialogs.account_import_source_db_pass_hint')}</small>
    </section>
  );
}
