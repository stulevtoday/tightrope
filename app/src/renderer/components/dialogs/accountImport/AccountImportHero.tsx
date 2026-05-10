import { useTranslation } from 'react-i18next';

export function AccountImportHero() {
  const { t } = useTranslation();
  return (
    <section className="account-import-hero">
      <p className="account-import-eyebrow">{t('dialogs.account_import_hero_eyebrow')}</p>
      <strong>{t('dialogs.account_import_hero_title')}</strong>
      <span>{t('dialogs.account_import_hero_desc')}</span>
      <div className="account-import-flow">
        <span>{t('dialogs.account_import_hero_step1')}</span>
        <span>{t('dialogs.account_import_hero_step2')}</span>
        <span>{t('dialogs.account_import_hero_step3')}</span>
      </div>
    </section>
  );
}
