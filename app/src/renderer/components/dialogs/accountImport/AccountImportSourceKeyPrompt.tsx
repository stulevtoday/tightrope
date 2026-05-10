import { useTranslation } from 'react-i18next';

interface AccountImportSourceKeyPromptProps {
  value: string;
  required: boolean;
  disabled: boolean;
  onChange: (value: string) => void;
}

export function AccountImportSourceKeyPrompt({
  value,
  required,
  disabled,
  onChange,
}: AccountImportSourceKeyPromptProps) {
  const { t } = useTranslation();
  return (
    <section className="account-import-source-key">
      <div className="account-import-source-key-header">
        <strong>{t('dialogs.account_import_source_key_title')}</strong>
        {required && <span>{t('dialogs.account_import_source_key_required')}</span>}
      </div>
      <input
        className="dock-input account-import-source-key-input mono"
        type="password"
        spellCheck={false}
        autoComplete="off"
        placeholder={t('dialogs.account_import_source_key_placeholder')}
        value={value}
        disabled={disabled}
        onChange={(event) => onChange(event.target.value)}
      />
      <small>{t('dialogs.account_import_source_key_hint')}</small>
    </section>
  );
}
