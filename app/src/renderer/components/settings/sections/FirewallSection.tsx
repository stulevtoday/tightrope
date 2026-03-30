import type { FirewallIpEntry, FirewallMode } from '../../../shared/types';

interface FirewallSectionProps {
  mode: FirewallMode;
  entries: FirewallIpEntry[];
  draftIpAddress: string;
  onSetDraftIpAddress: (value: string) => void;
  onAddIpAddress: () => void;
  onRemoveIpAddress: (ipAddress: string) => void;
}

export function FirewallSection({
  mode,
  entries,
  draftIpAddress,
  onSetDraftIpAddress,
  onAddIpAddress,
  onRemoveIpAddress,
}: FirewallSectionProps) {
  const modeLabel = mode === 'allowlist_active' ? `Allowlist active (${entries.length} IPs)` : 'Allow-all mode (0 IPs)';

  return (
    <div className="settings-group">
      <div className="settings-group-header">
        <h3>IP allowlist</h3>
        <p>When entries exist, only listed IPs can reach the proxy. Empty list allows all traffic.</p>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Firewall mode</strong>
        </div>
        <span style={{ fontSize: '12px', color: 'var(--text-secondary)' }}>{modeLabel}</span>
      </div>
      <div className="ip-list">
        {entries.length === 0 ? (
          <div className="ip-item">
            <span style={{ color: 'var(--text-secondary)' }}>No allowlist entries configured</span>
          </div>
        ) : (
          entries.map((entry) => (
            <div className="ip-item" key={entry.ipAddress}>
              <span>{entry.ipAddress}</span>
              <button
                className="btn-danger"
                type="button"
                style={{ fontSize: '11px', padding: '0.15rem 0.4rem' }}
                onClick={() => onRemoveIpAddress(entry.ipAddress)}
              >
                Remove
              </button>
            </div>
          ))
        )}
      </div>
      <div className="setting-row" style={{ borderBottom: 'none' }}>
        <div className="setting-label">
          <strong>Add IP</strong>
          <span>IPv4 or IPv6 address</span>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.35rem' }}>
          <input
            className="setting-input"
            type="text"
            placeholder="203.0.113.10"
            value={draftIpAddress}
            onChange={(event) => onSetDraftIpAddress(event.target.value)}
            style={{ width: '180px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
          />
          <button className="btn-secondary" type="button" onClick={onAddIpAddress}>
            Add
          </button>
        </div>
      </div>
    </div>
  );
}
