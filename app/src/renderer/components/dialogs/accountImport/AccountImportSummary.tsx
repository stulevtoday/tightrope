import { useTranslation } from 'react-i18next';
import type { SqlImportPreviewResponse } from '../../../shared/types';

interface AccountImportSummaryProps {
  preview: SqlImportPreviewResponse;
}

function formatFileSize(bytes: number): string {
  if (!Number.isFinite(bytes) || bytes <= 0) return '0 B';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let value = bytes;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit += 1;
  }
  const precision = value >= 100 || unit === 0 ? 0 : value >= 10 ? 1 : 2;
  return `${value.toFixed(precision)} ${units[unit]}`;
}

function formatLastModified(lastModifiedMs: number): string {
  if (!Number.isFinite(lastModifiedMs) || lastModifiedMs <= 0) return 'unknown';
  return new Date(lastModifiedMs).toLocaleString();
}

export function AccountImportSummary({ preview }: AccountImportSummaryProps) {
  const { t } = useTranslation();
  return (
    <section className="account-import-summary">
      <div className="account-import-kpis">
        <div className="account-import-kpi">
          <span>{t('dialogs.account_import_summary_total_rows')}</span>
          <strong>{preview.totals.scanned}</strong>
        </div>
        <div className="account-import-kpi">
          <span>{t('dialogs.account_import_summary_new')}</span>
          <strong>{preview.totals.newCount}</strong>
        </div>
        <div className="account-import-kpi">
          <span>{t('dialogs.account_import_summary_update')}</span>
          <strong>{preview.totals.updateCount}</strong>
        </div>
        <div className="account-import-kpi">
          <span>{t('dialogs.account_import_summary_skip')}</span>
          <strong>{preview.totals.skipCount}</strong>
        </div>
        <div className="account-import-kpi">
          <span>{t('dialogs.account_import_summary_invalid')}</span>
          <strong>{preview.totals.invalidCount}</strong>
        </div>
      </div>
      <div className="account-import-source">
        <div>
          <span>{t('dialogs.account_import_summary_source_db')}</span>
          <strong>{preview.source.fileName || '-'}</strong>
        </div>
        <div>
          <span>{t('dialogs.account_import_summary_last_modified')}</span>
          <strong>{formatLastModified(preview.source.modifiedAtMs)}</strong>
        </div>
        <div>
          <span>{t('dialogs.account_import_summary_file_size')}</span>
          <strong>{formatFileSize(preview.source.sizeBytes)}</strong>
        </div>
        <div>
          <span>{t('dialogs.account_import_summary_schema')}</span>
          <strong>{preview.source.schemaFingerprint || '-'}</strong>
        </div>
      </div>
    </section>
  );
}
