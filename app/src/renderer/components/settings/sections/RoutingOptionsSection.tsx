import type { UpstreamStreamTransport } from '../../../shared/types';

interface RoutingOptionsSectionProps {
  upstreamStreamTransport: UpstreamStreamTransport;
  stickyThreadsEnabled: boolean;
  preferEarlierResetAccounts: boolean;
  openaiCacheAffinityMaxAgeSeconds: number;
  onSetUpstreamStreamTransport: (transport: UpstreamStreamTransport) => void;
  onSetStickyThreadsEnabled: (enabled: boolean) => void;
  onSetPreferEarlierResetAccounts: (enabled: boolean) => void;
  onSetOpenaiCacheAffinityMaxAgeSeconds: (seconds: number) => void;
}

export function RoutingOptionsSection({
  upstreamStreamTransport,
  stickyThreadsEnabled,
  preferEarlierResetAccounts,
  openaiCacheAffinityMaxAgeSeconds,
  onSetUpstreamStreamTransport,
  onSetStickyThreadsEnabled,
  onSetPreferEarlierResetAccounts,
  onSetOpenaiCacheAffinityMaxAgeSeconds,
}: RoutingOptionsSectionProps) {
  return (
    <div className="settings-group">
      <div className="settings-group-header">
        <h3>Routing options</h3>
        <p>Transport and request affinity</p>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Upstream stream transport</strong>
          <span>Connection method for streaming responses</span>
        </div>
        <select
          className="setting-select"
          value={upstreamStreamTransport}
          onChange={(event) => onSetUpstreamStreamTransport(event.target.value as UpstreamStreamTransport)}
        >
          <option value="default">Server default</option>
          <option value="auto">Auto-detect</option>
          <option value="http">HTTP Responses API</option>
          <option value="websocket">WebSocket</option>
        </select>
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Sticky threads</strong>
          <span>Pin related requests to the same upstream account</span>
        </div>
        <button
          className={`setting-toggle${stickyThreadsEnabled ? ' on' : ''}`}
          type="button"
          aria-label="Toggle sticky threads"
          onClick={() => onSetStickyThreadsEnabled(!stickyThreadsEnabled)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Prefer earlier reset</strong>
          <span>Bias traffic toward accounts whose quota resets sooner</span>
        </div>
        <button
          className={`setting-toggle${preferEarlierResetAccounts ? ' on' : ''}`}
          type="button"
          aria-label="Toggle prefer earlier reset"
          onClick={() => onSetPreferEarlierResetAccounts(!preferEarlierResetAccounts)}
        />
      </div>
      <div className="setting-row">
        <div className="setting-label">
          <strong>Prompt cache affinity TTL</strong>
          <span>How long prompt-cache sticky mappings stay alive</span>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.3rem' }}>
          <input
            className="setting-input"
            type="number"
            value={openaiCacheAffinityMaxAgeSeconds}
            min={1}
            onChange={(event) => onSetOpenaiCacheAffinityMaxAgeSeconds(Math.max(1, Number(event.target.value) || 1))}
          />
          <span style={{ fontSize: '11px', color: 'var(--text-secondary)' }}>sec</span>
        </div>
      </div>
    </div>
  );
}
