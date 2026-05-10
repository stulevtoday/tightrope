import { useTranslation } from 'react-i18next';
import type { SqlImportApplyResponse } from '../../../shared/types';

interface AccountImportResultProps {
  result: SqlImportApplyResponse;
}

export function AccountImportResult({ result }: AccountImportResultProps) {
  const { t } = useTranslation();
  return (
    <section className="account-import-result">
      <div className="account-import-preview-header">
        <strong>{t('dialogs.account_import_result_title')}</strong>
        <span>{t('dialogs.account_import_result_completed')}</span>
      </div>
      <div className="account-import-result-grid">
        <div className="account-import-kpi">
          <span>{t('dialogs.account_import_result_scanned')}</span>
          <strong>{result.totals.scanned}</strong>
        </div>
        <div className="account-import-kpi">
          <span>{t('dialogs.account_import_result_inserted')}</span>
          <strong>{result.totals.inserted}</strong>
        </div>
        <div className="account-import-kpi">
          <span>{t('dialogs.account_import_result_updated')}</span>
          <strong>{result.totals.updated}</strong>
        </div>
        <div className="account-import-kpi">
          <span>{t('dialogs.account_import_result_skipped')}</span>
          <strong>{result.totals.skipped}</strong>
        </div>
        <div className="account-import-kpi">
          <span>{t('dialogs.account_import_result_invalid')}</span>
          <strong>{result.totals.invalid}</strong>
        </div>
        <div className="account-import-kpi">
          <span>{t('dialogs.account_import_result_failed')}</span>
          <strong>{result.totals.failed}</strong>
        </div>
      </div>
      {result.warnings.length > 0 && (
        <ul className="account-import-warning-list">
          {result.warnings.map((warning, index) => (
            <li key={`${warning}-${index}`}>{warning}</li>
          ))}
        </ul>
      )}
    </section>
  );
}
