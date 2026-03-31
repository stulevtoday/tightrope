// Minimal preload API — exposes safe IPC methods to the renderer
import { contextBridge, ipcRenderer } from 'electron';

contextBridge.exposeInMainWorld('tightrope', {
  platform: process.platform,
  getHealth: () => ipcRenderer.invoke('health'),
  getSettings: () => ipcRenderer.invoke('settings:get'),
  updateSettings: (update: unknown) => ipcRenderer.invoke('settings:update', update),
  oauthStart: (payload?: unknown) => ipcRenderer.invoke('oauth:start', payload),
  oauthStatus: () => ipcRenderer.invoke('oauth:status'),
  oauthStop: () => ipcRenderer.invoke('oauth:stop'),
  oauthRestart: () => ipcRenderer.invoke('oauth:restart'),
  oauthComplete: (payload: unknown) => ipcRenderer.invoke('oauth:complete', payload),
  oauthManualCallback: (callbackUrl: string) => ipcRenderer.invoke('oauth:manual-callback', callbackUrl),
  onOauthDeepLink: (listener: (event: { kind: 'success' | 'callback'; url: string }) => void) => {
    const handler = (_event: unknown, payload: unknown) => {
      if (!payload || typeof payload !== 'object') {
        return;
      }
      const maybeKind = (payload as { kind?: unknown }).kind;
      const maybeUrl = (payload as { url?: unknown }).url;
      if ((maybeKind === 'success' || maybeKind === 'callback') && typeof maybeUrl === 'string') {
        listener({ kind: maybeKind, url: maybeUrl });
      }
    };
    ipcRenderer.on('oauth:deep-link', handler);
    return () => {
      ipcRenderer.removeListener('oauth:deep-link', handler);
    };
  },
  listAccounts: () => ipcRenderer.invoke('accounts:list'),
  listRequestLogs: (payload?: { limit?: number; offset?: number }) => ipcRenderer.invoke('logs:list', payload),
  listAccountTraffic: () => ipcRenderer.invoke('accounts:traffic'),
  importAccount: (payload: unknown) => ipcRenderer.invoke('accounts:import', payload),
  pauseAccount: (accountId: string) => ipcRenderer.invoke('accounts:pause', accountId),
  reactivateAccount: (accountId: string) => ipcRenderer.invoke('accounts:reactivate', accountId),
  deleteAccount: (accountId: string) => ipcRenderer.invoke('accounts:delete', accountId),
  refreshAccountUsageTelemetry: (accountId: string) => ipcRenderer.invoke('accounts:refresh-usage', accountId),
  listFirewallIps: () => ipcRenderer.invoke('firewall:list'),
  addFirewallIp: (ipAddress: string) => ipcRenderer.invoke('firewall:add', ipAddress),
  removeFirewallIp: (ipAddress: string) => ipcRenderer.invoke('firewall:remove', ipAddress),
  clusterEnable: (config: unknown) => ipcRenderer.invoke('cluster:enable', config),
  clusterDisable: () => ipcRenderer.invoke('cluster:disable'),
  getClusterStatus: () => ipcRenderer.invoke('cluster:status'),
  addPeer: (address: string) => ipcRenderer.invoke('cluster:add-peer', address),
  removePeer: (siteId: string) => ipcRenderer.invoke('cluster:remove-peer', siteId),
  triggerSync: () => ipcRenderer.invoke('sync:trigger'),
  rollbackSyncBatch: (batchId: string) => ipcRenderer.invoke('sync:rollback-batch', batchId),
  windowMinimize: () => ipcRenderer.invoke('window:minimize'),
  windowToggleMaximize: () => ipcRenderer.invoke('window:toggle-maximize'),
  windowClose: () => ipcRenderer.invoke('window:close'),
  windowIsMaximized: () => ipcRenderer.invoke('window:is-maximized'),
});
