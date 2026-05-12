// IPC handlers: renderer <-> main <-> native module
import fs from 'node:fs';
import path from 'node:path';
import { app, BrowserWindow, dialog, ipcMain } from 'electron';
import { native } from './native';
import { resolveDatabasePath, savePassphraseToKeychain as savePassphrase, deleteSavedPassphrase as deletePassphrase } from './databasePassphrase';

const runtimeBaseUrl = process.env.TIGHTROPE_RUNTIME_URL ?? 'http://127.0.0.1:2455';

interface RuntimeErrorPayload {
  error?: {
    code?: string;
    message?: string;
  };
  message?: string;
}

interface RuntimeRequestError extends Error {
  code?: string;
}

interface OauthManualCallbackResult {
  status: string;
  errorMessage?: string | null;
}

interface RuntimeProxyState {
  enabled: boolean;
}

interface RuntimeAccountsListIpcResponse {
  accounts: unknown[];
  error?: {
    code?: string;
    message: string;
  };
}

function resolveBuildChannel(): 'dev' | 'debug' | 'release' {
  if (process.env.VITE_DEV_SERVER_URL) {
    return 'dev';
  }
  const buildType = (process.env.TIGHTROPE_BUILD_TYPE ?? process.env.BUILD_TYPE ?? '').toLowerCase();
  if (buildType === 'debug') {
    return 'debug';
  }
  const nodeEnv = (process.env.NODE_ENV ?? '').toLowerCase();
  if (nodeEnv === 'development' || nodeEnv === 'debug') {
    return 'debug';
  }
  return app.isPackaged ? 'release' : 'debug';
}

function isLeaderLinearizableReadError(error: unknown): boolean {
  if (!(error instanceof Error)) {
    return false;
  }
  const maybeCode = (error as RuntimeRequestError).code;
  if (maybeCode === 'linearizable_read_requires_leader') {
    return true;
  }
  return error.message.toLowerCase().includes('requires cluster leader');
}

async function runtimeRequest<T>(
  path: string,
  init?: { method?: string; headers?: Record<string, string>; body?: string },
  timeoutMs = 10000,
): Promise<T> {
  const fetchFn = (globalThis as unknown as { fetch?: (input: string, init?: unknown) => Promise<{
    ok: boolean;
    status: number;
    json: () => Promise<unknown>;
  }> }).fetch;
  if (!fetchFn) {
    throw new Error('Runtime HTTP bridge unavailable');
  }

  const controller = timeoutMs > 0 && typeof AbortController === 'function' ? new AbortController() : null;
  const timeout = controller
    ? setTimeout(() => {
        controller.abort();
      }, timeoutMs)
    : null;

  try {
    const requestInit = controller ? { ...(init ?? {}), signal: controller.signal } : init;
    const response = await fetchFn(`${runtimeBaseUrl}${path}`, requestInit);
    const payload = (await response.json().catch(() => null)) as T | RuntimeErrorPayload | null;
    if (response.ok && payload !== null) {
      return payload as T;
    }

    const errorMessage =
      (payload as RuntimeErrorPayload | null)?.error?.message ??
      (payload as RuntimeErrorPayload | null)?.message ??
      `HTTP ${response.status}`;
    const error = new Error(errorMessage) as RuntimeRequestError;
    error.code = (payload as RuntimeErrorPayload | null)?.error?.code;
    throw error;
  } catch (error) {
    if (error instanceof Error && error.name === 'AbortError') {
      throw new Error(`Runtime request timed out after ${timeoutMs}ms`);
    }
    throw error;
  } finally {
    if (timeout !== null) {
      clearTimeout(timeout);
    }
  }
}

export async function submitOauthManualCallback(callbackUrl: string): Promise<OauthManualCallbackResult> {
  return runtimeRequest<OauthManualCallbackResult>('/api/oauth/manual-callback', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ callbackUrl }),
  });
}

export function registerIpcHandlers(): void {
  ipcMain.handle('app:get-meta', () => ({
    version: app.getVersion(),
    buildChannel: resolveBuildChannel(),
    packaged: app.isPackaged,
  }));
  ipcMain.handle('health', () => native.getHealth());
  ipcMain.handle('settings:get', () => runtimeRequest('/api/settings', undefined, 30000));
  ipcMain.handle('settings:update', (_event, update: unknown) =>
    runtimeRequest('/api/settings', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(update ?? {}),
    }, 30000),
  );
  ipcMain.handle('database:change-passphrase', async (_event, payload: unknown) => {
    const result = await runtimeRequest('/api/settings/database/passphrase', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload ?? {}),
    }, 30000);
    const typedPayload = payload as { nextPassphrase?: string } | null;
    if (typedPayload?.nextPassphrase) {
      const dbPath = resolveDatabasePath();
      savePassphrase(dbPath, typedPayload.nextPassphrase);
    }
    return result;
  });
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
  ipcMain.handle('accounts:list', async () => {
    try {
      return await runtimeRequest('/api/accounts', undefined, 0);
    } catch (error) {
      if (isLeaderLinearizableReadError(error)) {
        return {
          accounts: [],
          error: {
            code: (error as RuntimeRequestError).code,
            message: error instanceof Error ? error.message : 'Linearizable read requires leader',
          },
        } satisfies RuntimeAccountsListIpcResponse;
      }
      throw error;
    }
  });
  ipcMain.handle('sessions:list', (_event, payload?: { limit?: number; offset?: number }) => {
    const limit = Number.isFinite(payload?.limit) ? Math.max(1, Math.trunc(payload?.limit ?? 0)) : 500;
    const offset = Number.isFinite(payload?.offset) ? Math.max(0, Math.trunc(payload?.offset ?? 0)) : 0;
    return runtimeRequest(`/api/sessions?limit=${limit}&offset=${offset}`, undefined, 0);
  });
  ipcMain.handle('sessions:purge-stale', () =>
    runtimeRequest('/api/sessions/purge-stale', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: '{}',
    }, 0),
  );
  ipcMain.handle('logs:list', (_event, payload?: { limit?: number; offset?: number }) => {
    const limit = Number.isFinite(payload?.limit) ? Math.max(1, Math.trunc(payload?.limit ?? 0)) : 200;
    const offset = Number.isFinite(payload?.offset) ? Math.max(0, Math.trunc(payload?.offset ?? 0)) : 0;
    return runtimeRequest(`/api/logs?limit=${limit}&offset=${offset}`, undefined, 0);
  });
  ipcMain.handle('accounts:traffic', () => runtimeRequest('/api/accounts/traffic', undefined, 0));
  ipcMain.handle('accounts:import', (_event, payload: unknown) =>
    runtimeRequest('/api/accounts/import', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload ?? {}),
    }),
  );
  ipcMain.handle('accounts:import-sql-preview', (_event, payload: unknown) =>
    runtimeRequest('/api/accounts/import/sqlite/preview', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload ?? {}),
    }),
  );
  ipcMain.handle('accounts:import-sql-pick-source', async () => {
    const parentWindow = BrowserWindow.getFocusedWindow() ?? BrowserWindow.getAllWindows()[0];
    const result = await dialog.showOpenDialog(parentWindow ?? undefined, {
      title: 'Select SQLite Database',
      properties: ['openFile'],
      filters: [{ name: 'SQLite Database', extensions: ['sqlite', 'sqlite3', 'db'] }],
    });
    if (result.canceled || result.filePaths.length === 0) {
      return null;
    }
    return result.filePaths[0] ?? null;
  });
  ipcMain.handle('accounts:import-sql-apply', (_event, payload: unknown) =>
    runtimeRequest('/api/accounts/import/sqlite/apply', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload ?? {}),
    }),
  );
  ipcMain.handle('accounts:pin', (_event, accountId: string) =>
    runtimeRequest(`/api/accounts/${encodeURIComponent(accountId)}/pin`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: '{}',
    }),
  );
  ipcMain.handle('accounts:unpin', (_event, accountId: string) =>
    runtimeRequest(`/api/accounts/${encodeURIComponent(accountId)}/unpin`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: '{}',
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
    }, 45000).catch((error: unknown) => {
      const runtimeError = error as RuntimeRequestError;
      return {
        error: {
          code: runtimeError?.code ?? 'usage_refresh_failed',
          message: error instanceof Error ? error.message : 'Failed to refresh usage telemetry',
        },
      };
    }),
  );
  ipcMain.handle('accounts:refresh-token', (_event, accountId: string) =>
    runtimeRequest(`/api/accounts/${encodeURIComponent(accountId)}/refresh-token`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: '{}',
    }, 45000).catch((error: unknown) => {
      const runtimeError = error as RuntimeRequestError;
      return {
        error: {
          code: runtimeError?.code ?? 'token_refresh_failed',
          message: error instanceof Error ? error.message : 'Failed to refresh account token',
        },
      };
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

  ipcMain.handle('backend:status', () => runtimeRequest<RuntimeProxyState>('/api/runtime/proxy'));
  ipcMain.handle('backend:start', () =>
    runtimeRequest<RuntimeProxyState>('/api/runtime/proxy/start', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: '{}',
    }),
  );
  ipcMain.handle('backend:stop', () =>
    runtimeRequest<RuntimeProxyState>('/api/runtime/proxy/stop', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: '{}',
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

  ipcMain.handle('database:export', async () => {
    const parentWindow = BrowserWindow.getFocusedWindow() ?? BrowserWindow.getAllWindows()[0];
    const result = await dialog.showSaveDialog(parentWindow ?? undefined, {
      title: 'Export Database',
      defaultPath: 'tightrope-backup.db',
      filters: [{ name: 'SQLite Database', extensions: ['db', 'sqlite', 'sqlite3'] }],
    });
    if (result.canceled || !result.filePath) {
      return { success: false, error: 'cancelled' };
    }

    const dbPath = resolveDatabasePath();
    if (!fs.existsSync(dbPath)) {
      return { success: false, error: 'Database file not found' };
    }

    const destPath = result.filePath;
    const filesToCopy = [dbPath];
    for (const ext of ['-wal', '-shm']) {
      const sidecar = dbPath + ext;
      if (fs.existsSync(sidecar)) {
        filesToCopy.push(sidecar);
      }
    }

    try {
      const destDir = path.dirname(destPath);
      fs.mkdirSync(destDir, { recursive: true });
      for (const src of filesToCopy) {
        const suffix = src === dbPath ? '' : src.slice(dbPath.length);
        const finalDest = destPath + suffix;
        fs.copyFileSync(src, finalDest);
      }
      return { success: true };
    } catch (err: unknown) {
      const message = err instanceof Error ? err.message : String(err);
      return { success: false, error: message };
    }
  });
}
