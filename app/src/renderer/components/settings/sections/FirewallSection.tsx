import type { FirewallIpEntry, FirewallMode } from '../../../shared/types';
import { useTranslation } from 'react-i18next';

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
  const { t } = useTranslation();
  const modeLabel = mode === 'allowlist_active' ? t('settings.firewall_allowlist_active', { count: entries.length }) : t('settings.firewall_allow_all');

  return (
    <div className="settings-group">
      <div className="settings-group-header">
        <h3>{t('settings.firewall_title')}</h3>
        <p>{t('settings.firewall_desc')}</p>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.firewall_mode')}</strong>
        </div>
        <span style={{ fontSize: '12px', color: 'var(--text-secondary)' }}>{modeLabel}</span>
      </div>
      <div className="ip-list">
        {entries.length === 0 ? (
          <div className="ip-item">
            <span style={{ color: 'var(--text-secondary)' }}>{t('settings.firewall_no_entries')}</span>
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
{t('settings.firewall_remove')}
              </button>
            </div>
          ))
        )}
      </div>
      <div className="setting-row" style={{ borderBottom: 'none' }}>
        <div className="setting-label">
          <strong>{t('settings.firewall_add_ip')}</strong>
          <span>{t('settings.firewall_add_ip_desc')}</span>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.35rem' }}>
          <input
            className="setting-input"
            type="text"
            placeholder={t('settings.firewall_add_ip_placeholder')}
            value={draftIpAddress}
            onChange={(event) => onSetDraftIpAddress(event.target.value)}
            style={{ width: '180px', textAlign: 'right', fontFamily: "'SF Mono',ui-monospace,monospace" }}
          />
          <button className="btn-secondary" type="button" onClick={onAddIpAddress}>
{t('settings.firewall_add_button')}
          </button>
        </div>
      </div>
    </div>
  );
}
