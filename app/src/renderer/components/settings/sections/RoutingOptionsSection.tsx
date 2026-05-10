import type { UpstreamStreamTransport } from '../../../shared/types';
import { useTranslation } from 'react-i18next';

interface RoutingOptionsSectionProps {
  upstreamStreamTransport: UpstreamStreamTransport;
  stickyThreadsEnabled: boolean;
  preferEarlierResetAccounts: boolean;
  strictLockPoolContinuations: boolean;
  openaiCacheAffinityMaxAgeSeconds: number;
  onSetUpstreamStreamTransport: (transport: UpstreamStreamTransport) => void;
  onSetStickyThreadsEnabled: (enabled: boolean) => void;
  onSetPreferEarlierResetAccounts: (enabled: boolean) => void;
  onSetStrictLockPoolContinuations: (enabled: boolean) => void;
  onSetOpenaiCacheAffinityMaxAgeSeconds: (seconds: number) => void;
}

export function RoutingOptionsSection({
  upstreamStreamTransport,
  stickyThreadsEnabled,
  preferEarlierResetAccounts,
  strictLockPoolContinuations,
  openaiCacheAffinityMaxAgeSeconds,
  onSetUpstreamStreamTransport,
  onSetStickyThreadsEnabled,
  onSetPreferEarlierResetAccounts,
  onSetStrictLockPoolContinuations,
  onSetOpenaiCacheAffinityMaxAgeSeconds,
}: RoutingOptionsSectionProps) {
  const { t } = useTranslation();
  return (
    <div className="settings-group">
      <div className="settings-group-header">
        <h3>{t('settings.routing_options_title')}</h3>
        <p>{t('settings.routing_options_desc')}</p>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.upstream_stream_transport')}</strong>
          <span>{t('settings.upstream_stream_transport_desc')}</span>
        </div>
        <select
          className="setting-select"
          value={upstreamStreamTransport}
          onChange={(event) => onSetUpstreamStreamTransport(event.target.value as UpstreamStreamTransport)}
        >
          <option value="default">{t('settings.transport_server_default')}</option>
          <option value="auto">{t('settings.transport_auto')}</option>
          <option value="http">{t('settings.transport_http')}</option>
          <option value="websocket">{t('settings.transport_websocket')}</option>
        </select>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.sticky_threads')}</strong>
          <span>{t('settings.sticky_threads_desc')}</span>
        </div>
        <button
          className={`setting-toggle${stickyThreadsEnabled ? ' on' : ''}`}
          type="button"
          aria-label={t('settings.sticky_threads_aria')}
          onClick={() => onSetStickyThreadsEnabled(!stickyThreadsEnabled)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.prefer_earlier_reset')}</strong>
          <span>{t('settings.prefer_earlier_reset_desc')}</span>
        </div>
        <button
          className={`setting-toggle${preferEarlierResetAccounts ? ' on' : ''}`}
          type="button"
          aria-label={t('settings.prefer_earlier_reset_aria')}
          onClick={() => onSetPreferEarlierResetAccounts(!preferEarlierResetAccounts)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.strict_lock_pool')}</strong>
          <span>{t('settings.strict_lock_pool_desc')}</span>
        </div>
        <button
          className={`setting-toggle${strictLockPoolContinuations ? ' on' : ''}`}
          type="button"
          aria-label={t('settings.strict_lock_pool_aria')}
          onClick={() => onSetStrictLockPoolContinuations(!strictLockPoolContinuations)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>{t('settings.cache_affinity_ttl')}</strong>
          <span>{t('settings.cache_affinity_ttl_desc')}</span>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.3rem' }}>
          <input
            className="setting-input"
            type="number"
            value={openaiCacheAffinityMaxAgeSeconds}
            min={1}
            onChange={(event) => onSetOpenaiCacheAffinityMaxAgeSeconds(Math.max(1, Number(event.target.value) || 1))}
          />
          <span style={{ fontSize: '11px', color: 'var(--text-secondary)' }}>{t('settings.cache_affinity_ttl_suffix')}</span>
        </div>
      </div>
    </div>
  );
}
