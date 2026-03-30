import type { Account, RouteRow } from '../../shared/types';
import { statusClass } from '../../state/logic';

interface LogsPageProps {
  visible: boolean;
  rows: RouteRow[];
  accounts: Account[];
  formatNumber: (value: number) => string;
  onOpenDrawer: (rowId: string) => void;
}

export function LogsPage({ visible, rows, accounts, formatNumber, onOpenDrawer }: LogsPageProps) {
  if (!visible) return null;

  return (
    <section className="logs-page page active" id="pageLogs" data-page="logs">
      <div className="logs-content">
        <header className="section-header">
          <div>
            <p className="eyebrow">Request</p>
            <h2>Logs</h2>
          </div>
        </header>
        <div className="table-wrap">
          <table>
            <thead>
              <tr>
                <th>Time</th>
                <th>Request</th>
                <th>Protocol</th>
                <th>Model</th>
                <th>Account</th>
                <th>Tokens</th>
                <th>Latency</th>
                <th>Status</th>
              </tr>
            </thead>
            <tbody>
              {rows.map((row) => {
                const accountName = accounts.find((account) => account.id === row.accountId)?.name ?? row.accountId;
                return (
                  <tr key={row.id} className="route-row" style={{ cursor: 'pointer' }} onClick={() => onOpenDrawer(row.id)}>
                    <td>{row.time}</td>
                    <td className="mono">{row.id}</td>
                    <td>{row.protocol}</td>
                    <td className="model-cell">{row.model}</td>
                    <td className="account-cell">{accountName}</td>
                    <td>{formatNumber(row.tokens)}</td>
                    <td>{row.latency} ms</td>
                    <td>
                      <span className={`status-badge ${statusClass(row.status)}`}>{row.status}</span>
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
