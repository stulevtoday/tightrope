import { useTranslation } from 'react-i18next';
import type { SqlImportAction, SqlImportPreviewRow } from '../../../shared/types';

interface AccountImportPreviewTableProps {
  rows: SqlImportPreviewRow[];
  overrides: Record<string, SqlImportAction>;
  disableActions?: boolean;
  importWithoutOverwrite: boolean;
  onOverrideChange: (sourceRowId: string, action: SqlImportAction) => void;
}

function badgeClass(action: SqlImportAction): string {
  return `account-import-badge ${action}`;
}

function authLabel(row: SqlImportPreviewRow): string {
  const parts: string[] = [];
  if (row.hasAccessToken) parts.push('access');
  if (row.hasRefreshToken) parts.push('refresh');
  if (row.hasIdToken) parts.push('id');
  return parts.length > 0 ? parts.join(' + ') : 'none';
}

export function AccountImportPreviewTable({
  rows,
  overrides,
  disableActions = false,
  importWithoutOverwrite,
  onOverrideChange,
}: AccountImportPreviewTableProps) {
  const { t } = useTranslation();
  return (
    <section className="account-import-preview">
      <div className="account-import-preview-header">
        <strong>{t('dialogs.account_import_preview_title')}</strong>
        <span>{importWithoutOverwrite ? t('dialogs.account_import_preview_no_overwrite') : t('dialogs.account_import_preview_overwrite')}</span>
      </div>
      <div className="account-import-table-wrap">
        <table className="account-import-table">
          <thead>
            <tr>
              <th>{t('dialogs.account_import_preview_email')}</th>
              <th>{t('dialogs.account_import_preview_provider')}</th>
              <th>{t('dialogs.account_import_preview_plan')}</th>
              <th>{t('dialogs.account_import_preview_detected_auth')}</th>
              <th>{t('dialogs.account_import_preview_action')}</th>
              <th>{t('dialogs.account_import_preview_reason')}</th>
            </tr>
          </thead>
          <tbody>
            {rows.map((row) => (
              <tr key={row.sourceRowId}>
                <td>{row.email ?? '—'}</td>
                <td>{row.provider ?? '—'}</td>
                <td>{row.planType ?? '—'}</td>
                <td>
                  <span className="account-import-auth">{authLabel(row)}</span>
                </td>
                <td>
                  {row.action === 'invalid' ? (
                    <span className={badgeClass('invalid')}>invalid</span>
                  ) : (
                    <select
                      className="account-import-action-select"
                      value={overrides[row.sourceRowId] ?? row.action}
                      onChange={(event) => onOverrideChange(row.sourceRowId, event.target.value as SqlImportAction)}
                      disabled={disableActions}
                    >
                      <option value="new">new</option>
                      <option value="update">update</option>
                      <option value="skip">skip</option>
                    </select>
                  )}
                </td>
                <td>
                  <span className={badgeClass(overrides[row.sourceRowId] ?? row.action)}>
                    {overrides[row.sourceRowId] ?? row.action}
                  </span>
                  <span className="account-import-reason">{row.reason}</span>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </section>
  );
}
