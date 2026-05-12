import { useTranslation } from 'react-i18next';
import { useAccountsContext, useLogsContext, useNavigationContext } from '../../state/context';
import { statusClass } from '../../state/logic';

export function LogsPage() {
  const { t } = useTranslation();
  const navigation = useNavigationContext();
  const accounts = useAccountsContext();
  const logs = useLogsContext();

  if (navigation.currentPage !== 'logs') return null;

  return (
    <section className="logs-page page active" id="pageLogs" data-page="logs">
      <div className="logs-content">
        <header className="section-header">
          <div>
            <p className="eyebrow">{t('logs.eyebrow')}</p>
            <h2>{t('logs.title')}</h2>
          </div>
        </header>
        <div className="table-wrap">
          <table>
            <thead>
              <tr>
                <th>{t('logs.col_time')}</th>
                <th>{t('logs.col_request')}</th>
                <th>{t('logs.col_protocol')}</th>
                <th>{t('logs.col_model')}</th>
                <th>{t('logs.col_account')}</th>
                <th>{t('logs.col_tokens')}</th>
                <th>{t('logs.col_latency')}</th>
                <th>{t('logs.col_status')}</th>
              </tr>
            </thead>
            <tbody>
              {logs.rows.map((row) => {
                const accountName = accounts.accounts.find((account) => account.id === row.accountId)?.name ?? row.accountId;
                const requestLabel = row.path ? `${row.method ?? 'POST'} ${row.path}` : row.id;
                return (
                  <tr key={row.id} className="route-row" style={{ cursor: 'pointer' }} onClick={() => logs.openDrawer(row.id)}>
                    <td>{row.time}</td>
                    <td className="mono">{requestLabel}</td>
                    <td>{row.protocol}</td>
                    <td className="model-cell">{row.model}</td>
                    <td className="account-cell">{accountName}</td>
                    <td>{accounts.formatNumber(row.tokens)}</td>
                    <td>{row.latency} ms</td>
                    <td>
                      <span className={`status-badge ${statusClass(row.status)}`}>{t(`common.status_${row.status}`)}</span>
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      </div>
    </section>
  );
}
