import { render, screen, waitFor, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { beforeEach, describe, expect, test, vi } from 'vitest';
import { App } from './App';
import { resetStatusNoticesForTests } from './state/statusNotices';
import type { DashboardSettings } from './shared/types';

describe('App', () => {
  beforeEach(() => {
    resetStatusNoticesForTests();
  });

  test('switches pages from the navigation rail', async () => {
    const user = userEvent.setup();
    render(<App />);

    expect(screen.getByRole('heading', { name: 'Routing pool' })).toBeInTheDocument();
    await user.click(screen.getByRole('button', { name: /Accounts/i }));
    expect(screen.getByRole('heading', { name: 'Accounts' })).toBeInTheDocument();

    await user.click(screen.getByRole('button', { name: /Sessions/i }));
    expect(screen.getByRole('heading', { name: 'Sessions' })).toBeInTheDocument();
  });

  test('opens backend dialog and performs start/stop actions', async () => {
    const user = userEvent.setup();
    render(<App />);

    await user.click(screen.getByRole('button', { name: 'Backend' }));
    expect(screen.getByRole('heading', { name: 'Backend' })).toBeInTheDocument();
    const dialog = screen.getByRole('dialog');

    await user.click(within(dialog).getByRole('button', { name: 'Stop' }));
    expect(screen.getAllByText('stopped').length).toBeGreaterThan(0);

    await user.click(within(dialog).getByRole('button', { name: 'Start' }));
    expect(screen.getAllByText('running').length).toBeGreaterThan(0);
  });

  test('wires import flow in add account dialog to native account APIs', async () => {
    const user = userEvent.setup();
    const importAccount = vi.fn(async ({ email, provider }: { email: string; provider: string }) => ({
      accountId: '11',
      email,
      provider,
      status: 'active',
    }));
    const listAccounts = vi
      .fn()
      .mockResolvedValueOnce({ accounts: [] })
      .mockResolvedValueOnce({
        accounts: [{ accountId: '11', email: 'imported@example.com', provider: 'openai', status: 'active' }],
      });
    window.tightrope = {
      ...window.tightrope,
      importAccount,
      listAccounts,
    };
    render(<App />);

    await user.click(screen.getAllByRole('button', { name: '+ Add' })[0]);
    expect(screen.getByRole('button', { name: /Import auth\.json/i })).toBeInTheDocument();

    await user.click(screen.getByRole('button', { name: /Import auth\.json/i }));
    const file = new File(['{"email":"imported@example.com","provider":"openai","token":"x"}'], 'auth.json', {
      type: 'application/json',
    });
    await user.upload(screen.getByLabelText(/Drop auth\.json here or click to browse/i), file);
    await user.click(screen.getByRole('button', { name: 'Import' }));

    await waitFor(() =>
      expect(importAccount).toHaveBeenCalledWith({
        email: 'imported@example.com',
        provider: 'openai',
      }),
    );
    await waitFor(() => expect(listAccounts).toHaveBeenCalledTimes(2));
    expect(await screen.findByText('Account added')).toBeInTheDocument();
    expect(document.querySelector('.success-check svg')).toBeInTheDocument();
  });

  test('account detail token status uses provider-managed labels', async () => {
    const user = userEvent.setup();
    const listAccounts = vi.fn(async () => ({
      accounts: [{ accountId: 'acc-1', email: 'alice@example.com', provider: 'openai', status: 'active' }],
    }));
    window.tightrope = {
      ...window.tightrope,
      listAccounts,
    };

    render(<App />);
    await user.click(screen.getByRole('button', { name: /Accounts/i }));
    await waitFor(() => expect(screen.getByRole('button', { name: /alice@example.com/i })).toBeInTheDocument());
    expect(screen.getByRole('button', { name: /Latency\s*—/i })).toBeInTheDocument();
    await user.click(screen.getByRole('button', { name: /alice@example.com/i }));

    expect(screen.getAllByText('managed by provider').length).toBeGreaterThan(0);
    expect(screen.queryByText(/expires in \d+m/i)).not.toBeInTheDocument();
    const requestUsageCard = screen.getByRole('heading', { name: 'Request usage' }).closest('.detail-card');
    expect(requestUsageCard).not.toBeNull();
    expect(within(requestUsageCard as HTMLElement).getAllByText('—').length).toBeGreaterThanOrEqual(4);
  });

  test('wires browser oauth manual callback flow to account refresh', async () => {
    const user = userEvent.setup();
    const oauthStart = vi.fn(async () => ({
      method: 'browser',
      authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=state-unit',
      callbackUrl: 'http://localhost:1455/auth/callback',
      verificationUrl: null,
      userCode: null,
      deviceAuthId: null,
      intervalSeconds: null,
      expiresInSeconds: null,
    }));
    const oauthManualCallback = vi.fn(async () => ({
      status: 'success',
      errorMessage: null,
    }));
    const listAccounts = vi
      .fn()
      .mockResolvedValueOnce({ accounts: [] })
      .mockResolvedValueOnce({
        accounts: [{ accountId: '12', email: 'alice@example.com', provider: 'openai', status: 'active' }],
      });
    window.tightrope = {
      ...window.tightrope,
      oauthStart,
      oauthManualCallback,
      listAccounts,
    };
    const openSpy = vi.spyOn(window, 'open').mockImplementation(() => null);

    render(<App />);
    await user.click(screen.getAllByRole('button', { name: '+ Add' })[0]);
    await user.click(screen.getByRole('button', { name: /Browser sign-in/i }));
    await user.click(screen.getByRole('button', { name: 'Open sign-in page' }));

    const callbackUrl = 'http://localhost:1455/auth/callback?code=abc123&state=state-unit&email=alice@example.com';
    await user.type(screen.getByPlaceholderText('http://127.0.0.1:1455/auth/callback?code=...'), callbackUrl);
    await user.click(screen.getByRole('button', { name: 'Submit' }));

    await waitFor(() => expect(oauthStart).toHaveBeenCalledWith({ forceMethod: 'browser' }));
    await waitFor(() => expect(oauthManualCallback).toHaveBeenCalledWith(callbackUrl));
    await waitFor(() => expect(listAccounts).toHaveBeenCalledTimes(2));
    expect(openSpy).toHaveBeenCalled();
    expect(await screen.findByText('Account added')).toBeInTheDocument();
    openSpy.mockRestore();
  });

  test('wires browser oauth popup callback success to account refresh automatically', async () => {
    const user = userEvent.setup();
    const oauthStart = vi.fn(async () => ({
      method: 'browser',
      authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=state-auto',
      callbackUrl: 'http://localhost:1455/auth/callback',
      verificationUrl: null,
      userCode: null,
      deviceAuthId: null,
      intervalSeconds: null,
      expiresInSeconds: null,
    }));
    const oauthStatus = vi
      .fn()
      .mockResolvedValueOnce({
        status: 'pending',
        errorMessage: null,
        listenerRunning: true,
        callbackUrl: 'http://localhost:1455/auth/callback',
        authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=state-auto',
      })
      .mockResolvedValueOnce({
        status: 'success',
        errorMessage: null,
        listenerRunning: false,
        callbackUrl: 'http://localhost:1455/auth/callback?code=ok&state=state-auto&email=popup@example.com',
        authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=state-auto',
      })
      .mockResolvedValue({
        status: 'success',
        errorMessage: null,
        listenerRunning: false,
        callbackUrl: 'http://localhost:1455/auth/callback?code=ok&state=state-auto&email=popup@example.com',
        authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=state-auto',
      });
    const listAccounts = vi
      .fn()
      .mockResolvedValueOnce({ accounts: [] })
      .mockResolvedValueOnce({
        accounts: [{ accountId: '14', email: 'popup@example.com', provider: 'openai', status: 'active' }],
      });
    const oauthManualCallback = vi.fn(async () => ({ status: 'success', errorMessage: null }));
    const importAccount = vi.fn(async ({ email, provider }: { email: string; provider: string }) => ({
      accountId: '14',
      email,
      provider,
      status: 'active',
    }));
    window.tightrope = {
      ...window.tightrope,
      oauthStart,
      oauthStatus,
      oauthManualCallback,
      importAccount,
      listAccounts,
    };
    const openSpy = vi.spyOn(window, 'open').mockImplementation(() => null);

    render(<App />);
    await user.click(screen.getAllByRole('button', { name: '+ Add' })[0]);
    await user.click(screen.getByRole('button', { name: /Browser sign-in/i }));
    await user.click(screen.getByRole('button', { name: 'Open sign-in page' }));

    await waitFor(() => expect(oauthStart).toHaveBeenCalledWith({ forceMethod: 'browser' }));
    await waitFor(() => expect(oauthStatus.mock.calls.length).toBeGreaterThanOrEqual(2));
    await waitFor(() => expect(listAccounts).toHaveBeenCalledTimes(2));
    expect(oauthManualCallback).not.toHaveBeenCalled();
    expect(importAccount).not.toHaveBeenCalled();
    expect(openSpy).toHaveBeenCalled();
    expect(await screen.findByText('Account added')).toBeInTheDocument();
    openSpy.mockRestore();
  });

  test('copies browser oauth URL to clipboard from add account dialog', async () => {
    const user = userEvent.setup();
    const writeText = vi.fn(async () => {});
    const oauthStart = vi.fn(async () => ({
      method: 'browser',
      authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=test',
      callbackUrl: 'http://localhost:1455/auth/callback',
      verificationUrl: null,
      userCode: null,
      deviceAuthId: null,
      intervalSeconds: null,
      expiresInSeconds: null,
    }));
    const oauthStatus = vi.fn(async () => ({
      status: 'pending',
      errorMessage: null,
      listenerRunning: true,
      callbackUrl: 'http://localhost:1455/auth/callback',
      authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=test',
    }));
    window.tightrope = {
      ...window.tightrope,
      oauthStart,
      oauthStatus,
    };
    Object.defineProperty(navigator, 'clipboard', {
      configurable: true,
      value: { writeText },
    });

    render(<App />);
    await user.click(screen.getAllByRole('button', { name: '+ Add' })[0]);
    await user.click(screen.getByRole('button', { name: /Browser sign-in/i }));
    await user.click(screen.getByRole('button', { name: 'Copy' }));

    await waitFor(() =>
      expect(writeText).toHaveBeenCalledWith('https://auth.openai.com/oauth/authorize?response_type=code&state=test'),
    );
  });

  test('auto-generates browser oauth URL on app load when status lacks one', async () => {
    const oauthStatus = vi.fn(async () => ({
      status: 'pending',
      errorMessage: null,
      listenerRunning: false,
      callbackUrl: 'http://localhost:1455/auth/callback',
      authorizationUrl: null,
    }));
    const oauthStart = vi.fn(async () => ({
      method: 'browser',
      authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=auto-init',
      callbackUrl: 'http://localhost:1455/auth/callback',
      verificationUrl: null,
      userCode: null,
      deviceAuthId: null,
      intervalSeconds: null,
      expiresInSeconds: null,
    }));
    window.tightrope = {
      ...window.tightrope,
      oauthStatus,
      oauthStart,
    };

    render(<App />);

    await waitFor(() => expect(oauthStatus).toHaveBeenCalled());
    await waitFor(() => expect(oauthStart).toHaveBeenCalledWith({ forceMethod: 'browser' }));
  });

  test('renders status bar and reflects runtime notices', async () => {
    const user = userEvent.setup();
    render(<App />);

    expect(screen.getByRole('status')).toBeInTheDocument();
    expect(screen.getByText('Ready')).toBeInTheDocument();

    await user.click(screen.getByRole('button', { name: 'Backend' }));
    const dialog = screen.getByRole('dialog');
    await user.click(within(dialog).getByRole('button', { name: 'Stop' }));

    expect(within(screen.getByRole('status')).getByText('backend stopped')).toBeInTheDocument();
  });

  test('wires routing options settings to native settings API', async () => {
    const user = userEvent.setup();
    const settings: DashboardSettings = {
      theme: 'auto',
      stickyThreadsEnabled: false,
      upstreamStreamTransport: 'default',
      preferEarlierResetAccounts: false,
      routingStrategy: 'usage_weighted',
      openaiCacheAffinityMaxAgeSeconds: 300,
      importWithoutOverwrite: false,
      totpRequiredOnLogin: false,
      totpConfigured: false,
      apiKeyAuthEnabled: false,
      routingHeadroomWeightPrimary: 0.35,
      routingHeadroomWeightSecondary: 0.65,
      routingScoreAlpha: 0.3,
      routingScoreBeta: 0.25,
      routingScoreGamma: 0.2,
      routingScoreDelta: 0.2,
      routingScoreZeta: 0.05,
      routingScoreEta: 1,
      routingSuccessRateRho: 2,
      syncClusterName: 'default',
      syncSiteId: 1,
      syncPort: 9400,
      syncDiscoveryEnabled: true,
      syncIntervalSeconds: 5,
      syncConflictResolution: 'lww',
      syncJournalRetentionDays: 30,
      syncTlsEnabled: true,
    };
    const updateSettings = vi.fn(async (update: Partial<DashboardSettings>) => ({ ...settings, ...update }));
    const getSettings = vi.fn(async () => ({ ...settings }));
    window.tightrope = {
      ...window.tightrope,
      getSettings,
      updateSettings,
    };

    render(<App />);
    await user.click(screen.getByRole('button', { name: /Settings/i }));

    const transportRow = screen.getByText('Upstream stream transport').closest('.setting-row');
    expect(transportRow).not.toBeNull();
    await user.selectOptions(within(transportRow as HTMLElement).getByRole('combobox'), 'websocket');
    await user.click(screen.getByRole('button', { name: 'Toggle sticky threads' }));
    await user.click(screen.getByRole('button', { name: 'Toggle prefer earlier reset' }));

    expect(getSettings).toHaveBeenCalled();
    expect(updateSettings).toHaveBeenCalled();
    expect(updateSettings.mock.calls.some(([payload]) => payload?.upstreamStreamTransport === 'websocket')).toBe(true);
    expect(updateSettings.mock.calls.some(([payload]) => payload?.stickyThreadsEnabled === true)).toBe(true);
    expect(updateSettings.mock.calls.some(([payload]) => payload?.preferEarlierResetAccounts === true)).toBe(true);
  });

  test('wires firewall and sync settings controls to native bridge', async () => {
    const user = userEvent.setup();
    const listFirewallIps = vi
      .fn()
      .mockResolvedValueOnce({ mode: 'allow_all', entries: [] })
      .mockResolvedValue({ mode: 'allowlist_active', entries: [{ ipAddress: '203.0.113.10', createdAt: 'now' }] });
    const addFirewallIp = vi.fn(async () => ({ ipAddress: '203.0.113.10', createdAt: 'now' }));
    const clusterEnable = vi.fn(async () => {});
    const addPeer = vi.fn(async () => {});
    const triggerSync = vi.fn(async () => {});
    window.tightrope = {
      ...window.tightrope,
      listFirewallIps,
      addFirewallIp,
      clusterEnable,
      addPeer,
      triggerSync,
    };

    render(<App />);
    await user.click(screen.getByRole('button', { name: /Settings/i }));

    await user.type(screen.getByPlaceholderText('203.0.113.10'), '203.0.113.10');
    await user.click(screen.getByRole('button', { name: 'Add' }));
    expect(addFirewallIp).toHaveBeenCalledWith('203.0.113.10');

    await user.click(screen.getByRole('button', { name: 'Toggle sync' }));
    expect(clusterEnable).toHaveBeenCalled();

    await user.type(screen.getByPlaceholderText('host:port'), '10.0.0.5:9400');
    await user.click(screen.getByRole('button', { name: 'Add peer' }));
    expect(addPeer).toHaveBeenCalledWith('10.0.0.5:9400');

    await user.click(screen.getByRole('button', { name: 'Trigger sync now' }));
    expect(triggerSync).toHaveBeenCalled();
  });

  test('wires account detail actions to native account APIs', async () => {
    const user = userEvent.setup();
    const listAccounts = vi
      .fn()
      .mockResolvedValueOnce({
        accounts: [{ accountId: '10', email: 'ops@example.com', provider: 'openai', status: 'active' }],
      })
      .mockResolvedValueOnce({
        accounts: [
          {
            accountId: '10',
            email: 'ops@example.com',
            provider: 'openai',
            status: 'active',
            quotaPrimaryPercent: 22,
            quotaSecondaryPercent: 61,
          },
        ],
      })
      .mockResolvedValueOnce({
        accounts: [{ accountId: '10', email: 'ops@example.com', provider: 'openai', status: 'paused' }],
      })
      .mockResolvedValueOnce({
        accounts: [{ accountId: '10', email: 'ops@example.com', provider: 'openai', status: 'active' }],
      })
      .mockResolvedValueOnce({
        accounts: [],
      });
    const pauseAccount = vi.fn(async () => ({ status: 'paused' }));
    const reactivateAccount = vi.fn(async () => ({ status: 'reactivated' }));
    const deleteAccount = vi.fn(async () => ({ status: 'deleted' }));
    const refreshAccountUsageTelemetry = vi.fn(async () => ({
      accountId: '10',
      email: 'ops@example.com',
      provider: 'openai',
      status: 'active',
      quotaPrimaryPercent: 22,
      quotaSecondaryPercent: 61,
    }));
    window.tightrope = {
      ...window.tightrope,
      listAccounts,
      pauseAccount,
      reactivateAccount,
      deleteAccount,
      refreshAccountUsageTelemetry,
    };

    render(<App />);
    await user.click(screen.getByRole('button', { name: /Accounts/i }));

    const accountRow = await screen.findByRole('button', { name: /ops@example\.com/i });
    await user.click(accountRow);

    await user.click(screen.getByRole('button', { name: 'Refresh usage' }));
    await waitFor(() => expect(refreshAccountUsageTelemetry).toHaveBeenCalledWith('10'));

    await user.click(screen.getByRole('button', { name: 'Pause' }));
    await waitFor(() => expect(pauseAccount).toHaveBeenCalledWith('10'));

    await user.click(await screen.findByRole('button', { name: 'Resume' }));
    await waitFor(() => expect(reactivateAccount).toHaveBeenCalledWith('10'));

    await user.click(screen.getByRole('button', { name: 'Delete' }));
    await waitFor(() => expect(deleteAccount).toHaveBeenCalledWith('10'));
  });

  test('wires oauth dialog lifecycle controls to native backend bridge', async () => {
    const user = userEvent.setup();
    const oauthStatus = vi.fn(async () => ({
      status: 'pending',
      errorMessage: null,
      listenerRunning: false,
      callbackUrl: 'http://localhost:1455/auth/callback',
      authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=init',
    }));
    const oauthStart = vi.fn(async () => ({
      method: 'browser',
      authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=start',
      callbackUrl: 'http://localhost:1455/auth/callback',
      verificationUrl: null,
      userCode: null,
      deviceAuthId: null,
      intervalSeconds: null,
      expiresInSeconds: null,
    }));
    const oauthStop = vi.fn(async () => ({
      status: 'stopped',
      errorMessage: null,
      listenerRunning: false,
      callbackUrl: 'http://localhost:1455/auth/callback',
      authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=start',
    }));
    const oauthRestart = vi.fn(async () => ({
      method: 'browser',
      authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=restart',
      callbackUrl: 'http://localhost:1455/auth/callback',
      verificationUrl: null,
      userCode: null,
      deviceAuthId: null,
      intervalSeconds: null,
      expiresInSeconds: null,
    }));

    const tightropeApi = window.tightrope as typeof window.tightrope & {
      oauthStatus: typeof oauthStatus;
      oauthStart: typeof oauthStart;
      oauthStop: typeof oauthStop;
      oauthRestart: typeof oauthRestart;
    };
    tightropeApi.oauthStatus = oauthStatus;
    tightropeApi.oauthStart = oauthStart;
    tightropeApi.oauthStop = oauthStop;
    tightropeApi.oauthRestart = oauthRestart;

    render(<App />);

    await user.click(screen.getByRole('button', { name: 'OAuth' }));
    const dialog = screen.getByRole('dialog');

    await user.click(within(dialog).getByRole('button', { name: 'Initialize' }));
    await waitFor(() => expect(oauthStatus).toHaveBeenCalledTimes(2));

    await user.click(within(dialog).getByRole('button', { name: 'Start Listener' }));
    await waitFor(() => expect(oauthStart).toHaveBeenCalledTimes(1));

    await user.click(await within(dialog).findByRole('button', { name: 'Stop Listener' }));
    await waitFor(() => expect(oauthStop).toHaveBeenCalledTimes(1));

    await user.click(within(dialog).getByRole('button', { name: 'Restart Listener' }));
    await waitFor(() => expect(oauthRestart).toHaveBeenCalledTimes(1));
  });
});
