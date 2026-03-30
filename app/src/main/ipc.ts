// IPC handlers: renderer <-> main <-> native module
import { BrowserWindow, ipcMain } from 'electron';
import { native } from './native';

const runtimeBaseUrl = process.env.TIGHTROPE_RUNTIME_URL ?? 'http://127.0.0.1:2455';

interface RuntimeErrorPayload {
  error?: {
    code?: string;
    message?: string;
  };
  message?: string;
}

interface OauthManualCallbackResult {
  status: string;
  errorMessage?: string | null;
}

async function runtimeRequest<T>(path: string, init?: { method?: string; headers?: Record<string, string>; body?: string }): Promise<T> {
  const fetchFn = (globalThis as unknown as { fetch?: (input: string, init?: unknown) => Promise<{
    ok: boolean;
    status: number;
    json: () => Promise<unknown>;
  }> }).fetch;
  if (!fetchFn) {
    throw new Error('Runtime HTTP bridge unavailable');
  }

  const response = await fetchFn(`${runtimeBaseUrl}${path}`, init);
  const payload = (await response.json().catch(() => null)) as T | RuntimeErrorPayload | null;
  if (response.ok && payload !== null) {
    return payload as T;
  }

  const errorMessage =
    (payload as RuntimeErrorPayload | null)?.error?.message ??
    (payload as RuntimeErrorPayload | null)?.message ??
    `HTTP ${response.status}`;
  throw new Error(errorMessage);
}

export async function submitOauthManualCallback(callbackUrl: string): Promise<OauthManualCallbackResult> {
  return runtimeRequest<OauthManualCallbackResult>('/api/oauth/manual-callback', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ callbackUrl }),
  });
}

export function registerIpcHandlers(): void {
  ipcMain.handle('health', () => native.getHealth());
  ipcMain.handle('settings:get', () => runtimeRequest('/api/settings'));
  ipcMain.handle('settings:update', (_event, update: unknown) =>
    runtimeRequest('/api/settings', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(update ?? {}),
    }),
  );
  ipcMain.handle('oauth:start', (_event, payload: unknown) =>
    runtimeRequest('/api/oauth/start', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload ?? {}),
    }),
  );
  ipcMain.handle('oauth:status', () => runtimeRequest('/api/oauth/status'));
  ipcMain.handle('oauth:stop', () =>
    runtimeRequest('/api/oauth/stop', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: '{}',
    }),
  );
  ipcMain.handle('oauth:restart', () =>
    runtimeRequest('/api/oauth/restart', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: '{}',
    }),
  );
  ipcMain.handle('oauth:complete', (_event, payload: unknown) =>
    runtimeRequest('/api/oauth/complete', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload ?? {}),
    }),
  );
  ipcMain.handle('oauth:manual-callback', (_event, callbackUrl: string) =>
    submitOauthManualCallback(callbackUrl),
  );
  ipcMain.handle('accounts:list', () => runtimeRequest('/api/accounts'));
  ipcMain.handle('accounts:import', (_event, payload: unknown) =>
    runtimeRequest('/api/accounts/import', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload ?? {}),
    }),
  );
  ipcMain.handle('accounts:pause', (_event, accountId: string) =>
    runtimeRequest(`/api/accounts/${encodeURIComponent(accountId)}/pause`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: '{}',
    }),
  );
  ipcMain.handle('accounts:reactivate', (_event, accountId: string) =>
    runtimeRequest(`/api/accounts/${encodeURIComponent(accountId)}/reactivate`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: '{}',
    }),
  );
  ipcMain.handle('accounts:delete', (_event, accountId: string) =>
    runtimeRequest(`/api/accounts/${encodeURIComponent(accountId)}`, {
      method: 'DELETE',
    }),
  );
  ipcMain.handle('accounts:refresh-usage', (_event, accountId: string) =>
    runtimeRequest(`/api/accounts/${encodeURIComponent(accountId)}/refresh-usage`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: '{}',
    }),
  );

  ipcMain.handle('firewall:list', () => runtimeRequest('/api/firewall/ips'));
  ipcMain.handle('firewall:add', (_event, ipAddress: string) =>
    runtimeRequest('/api/firewall/ips', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ipAddress }),
    }),
  );
  ipcMain.handle('firewall:remove', (_event, ipAddress: string) =>
    runtimeRequest(`/api/firewall/ips/${encodeURIComponent(ipAddress)}`, {
      method: 'DELETE',
    }),
  );

  ipcMain.handle('cluster:enable', (_event, config) => native.clusterEnable(config));
  ipcMain.handle('cluster:disable', () => native.clusterDisable());
  ipcMain.handle('cluster:status', () => native.clusterStatus());
  ipcMain.handle('cluster:add-peer', (_event, address: string) => native.clusterAddPeer(address));
  ipcMain.handle('cluster:remove-peer', (_event, siteId: string) => native.clusterRemovePeer(siteId));

  ipcMain.handle('sync:trigger', () => native.syncTriggerNow());
  ipcMain.handle('sync:rollback-batch', (_event, batchId: string) => native.syncRollbackBatch(batchId));

  ipcMain.handle('window:minimize', (event) => {
    BrowserWindow.fromWebContents(event.sender)?.minimize();
  });

  ipcMain.handle('window:toggle-maximize', (event) => {
    const window = BrowserWindow.fromWebContents(event.sender);
    if (!window) return;
    if (window.isMaximized()) {
      window.unmaximize();
      return;
    }
    window.maximize();
  });

  ipcMain.handle('window:close', (event) => {
    BrowserWindow.fromWebContents(event.sender)?.close();
  });

  ipcMain.handle('window:is-maximized', (event) => {
    return BrowserWindow.fromWebContents(event.sender)?.isMaximized() ?? false;
  });
}
