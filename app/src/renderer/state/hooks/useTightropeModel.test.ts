import { act, renderHook, waitFor } from '@testing-library/react';
import { describe, expect, test, vi } from 'vitest';
import type { TightropeService } from '../../services/tightrope';
import { useTightropeModel } from './useTightropeModel';
import { defaultDashboardSettings } from './useSettings';

function createServiceMocks(): TightropeService {
  return {
    getAppMetaRequest: vi.fn(async () => ({ version: '0.1.0', buildChannel: 'dev', packaged: false })),
    oauthStartRequest: vi.fn(async () => ({
      method: 'browser',
      authorizationUrl: null,
      callbackUrl: null,
      verificationUrl: null,
      userCode: null,
      deviceAuthId: null,
      intervalSeconds: null,
      expiresInSeconds: null,
    })),
    oauthStatusRequest: vi.fn(async () => ({
      status: 'idle',
      errorMessage: null,
      listenerRunning: false,
      callbackUrl: null,
      authorizationUrl: null,
    })),
    oauthStopRequest: vi.fn(async () => ({ status: 'stopped', errorMessage: null })),
    oauthRestartRequest: vi.fn(async () => ({
      method: 'browser',
      authorizationUrl: null,
      callbackUrl: null,
      verificationUrl: null,
      userCode: null,
      deviceAuthId: null,
      intervalSeconds: null,
      expiresInSeconds: null,
    })),
    oauthCompleteRequest: vi.fn(async () => ({ status: 'pending' })),
    oauthManualCallbackRequest: vi.fn(async () => ({ status: 'error', errorMessage: null })),
    listAccountsRequest: vi.fn(async () => []),
    listStickySessionsRequest: vi.fn(async () => ({ generatedAtMs: Date.now(), sessions: [] })),
    listRequestLogsRequest: vi.fn(async () => []),
    listAccountTrafficRequest: vi.fn(async () => []),
    backendStatusRequest: vi.fn(async () => ({ enabled: true })),
    backendStartRequest: vi.fn(async () => ({ enabled: true })),
    backendStopRequest: vi.fn(async () => ({ enabled: false })),
    importAccountRequest: vi.fn(async (email: string, provider: string) => ({
      accountId: 'imported',
      email,
      provider,
      status: 'active',
    })),
    previewSqlImportRequest: vi.fn(async () => ({
      source: {
        path: '/tmp/source.db',
        fileName: 'source.db',
        sizeBytes: 0,
        modifiedAtMs: 0,
        schemaFingerprint: 'accounts:',
      },
      totals: {
        scanned: 0,
        newCount: 0,
        updateCount: 0,
        skipCount: 0,
        invalidCount: 0,
      },
      rows: [],
      warnings: [],
    })),
    pickSqlImportSourcePathRequest: vi.fn(async () => null),
    applySqlImportRequest: vi.fn(async () => ({
      totals: {
        scanned: 0,
        inserted: 0,
        updated: 0,
        skipped: 0,
        invalid: 0,
        failed: 0,
      },
      warnings: [],
    })),
    pinAccountRequest: vi.fn(async () => {}),
    unpinAccountRequest: vi.fn(async () => {}),
    pauseAccountRequest: vi.fn(async () => {}),
    reactivateAccountRequest: vi.fn(async () => {}),
    deleteAccountRequest: vi.fn(async () => {}),
    refreshAccountUsageTelemetryRequest: vi.fn(async (accountId: string) => ({
      accountId,
      email: `${accountId}@test.local`,
      provider: 'openai',
      status: 'active',
    })),
    refreshAccountTokenRequest: vi.fn(async (accountId: string) => ({
      accountId,
      email: `${accountId}@test.local`,
      provider: 'openai',
      status: 'active',
    })),
    getSettingsRequest: vi.fn(async () => ({ ...defaultDashboardSettings })),
    updateSettingsRequest: vi.fn(async (update) => ({ ...defaultDashboardSettings, ...update })),
    changeDatabasePassphraseRequest: vi.fn(async () => {}),
    exportDatabaseRequest: vi.fn(async () => ({ success: true })),
    listFirewallIpsRequest: vi.fn(async () => ({ mode: 'allow_all' as const, entries: [] })),
    addFirewallIpRequest: vi.fn(async () => true),
    removeFirewallIpRequest: vi.fn(async () => true),
    getClusterStatusRequest: vi.fn(async () => null),
    clusterEnableRequest: vi.fn(async () => true),
    clusterDisableRequest: vi.fn(async () => true),
    addPeerRequest: vi.fn(async () => true),
    removePeerRequest: vi.fn(async () => true),
    triggerSyncRequest: vi.fn(async () => true),
    onOauthDeepLinkRequest: vi.fn(() => null),
    onAboutOpenRequest: vi.fn(() => null),
    onSyncEventRequest: vi.fn(() => null),
    platformRequest: vi.fn(() => 'darwin' as NodeJS.Platform),
    windowCloseRequest: vi.fn(async () => true),
    windowMinimizeRequest: vi.fn(async () => true),
    windowToggleMaximizeRequest: vi.fn(async () => true),
    windowIsMaximizedRequest: vi.fn(async () => false),
  };
}

describe('useTightropeModel', () => {
  test('exposes core model contract at initialization', async () => {
    const service = createServiceMocks();
    const { result } = renderHook(() => useTightropeModel(service));
    await waitFor(() => expect(result.current.state.currentPage).toBe('router'));

    expect(result.current.state.currentPage).toBe('router');
    expect(Array.isArray(result.current.accounts)).toBe(true);
    expect(Array.isArray(result.current.routingModes)).toBe(true);
    expect(typeof result.current.setCurrentPage).toBe('function');
    expect(typeof result.current.setTheme).toBe('function');
    expect(typeof result.current.saveSettingsChanges).toBe('function');
    expect(typeof result.current.setSearchQuery).toBe('function');
    expect(typeof result.current.openAddAccountDialog).toBe('function');
    expect(typeof result.current.setSessionsKindFilter).toBe('function');
    expect(typeof result.current.openDrawer).toBe('function');
  });

  test('applies unsaved-settings navigation guard and discard flow', async () => {
    const service = createServiceMocks();
    const { result } = renderHook(() => useTightropeModel(service));

    act(() => {
      result.current.setCurrentPage('settings');
    });
    expect(result.current.state.currentPage).toBe('settings');

    act(() => {
      result.current.setTheme('dark');
    });
    await waitFor(() => expect(result.current.hasUnsavedSettingsChanges).toBe(true));

    act(() => {
      result.current.setCurrentPage('accounts');
    });
    await waitFor(() => expect(result.current.settingsLeaveDialogOpen).toBe(true));
    expect(result.current.state.currentPage).toBe('settings');

    act(() => {
      result.current.discardSettingsAndNavigate();
    });
    await waitFor(() => expect(result.current.settingsLeaveDialogOpen).toBe(false));
    await waitFor(() => expect(result.current.state.currentPage).toBe('accounts'));
    await waitFor(() => expect(result.current.hasUnsavedSettingsChanges).toBe(false));
  });
});
