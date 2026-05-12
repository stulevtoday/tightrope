import { act, render, screen, waitFor, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { beforeEach, describe, expect, test, vi } from 'vitest';
import { App } from './App';
import { resetStatusNoticesForTests } from './state/statusNotices';
import type { DashboardSettings } from './shared/types';
import { makeTestService as makeService } from './test/makeService';

describe('App', () => {
  beforeEach(() => {
    resetStatusNoticesForTests();
  });

  test('switches pages from the navigation rail', async () => {
    const user = userEvent.setup();
    render(<App />);

    expect(screen.getByRole('heading', { name: 'Routing pool' })).toBeInTheDocument();
    await user.click(screen.getByRole('button', { name: /Trace/i }));
    expect(screen.getByRole('heading', { name: 'Request routing trace' })).toBeInTheDocument();

    await user.click(screen.getByRole('button', { name: /Accounts/i }));
    expect(screen.getByRole('heading', { name: 'Accounts' })).toBeInTheDocument();

    await user.click(screen.getByRole('button', { name: /Affinity/i }));
    expect(screen.getByRole('heading', { name: 'Account affinity' })).toBeInTheDocument();
  });

  test('hydrates sessions page from runtime sticky sessions', async () => {
    const user = userEvent.setup();
    const listStickySessions = vi.fn(async () => ({
      generatedAtMs: 1_800_000_000_000,
      sessions: [
        {
          sessionKey: 'turn_live_runtime',
          accountId: 'acc-live',
          updatedAtMs: 1_800_000_000_000,
          expiresAtMs: 1_800_000_060_000,
        },
      ],
    }));
    const listAccounts = vi.fn(async () => ({
      accounts: [{ accountId: 'acc-live', email: 'live@test.local', provider: 'openai', status: 'active' }],
    }));
    const service = makeService({
      listStickySessions,
      listAccounts,
    });

    render(<App service={service} />);
    await user.click(screen.getByRole('button', { name: /Affinity/i }));

    expect(await screen.findByTitle('turn_live_runtime')).toBeInTheDocument();
    await waitFor(() => expect(listStickySessions).toHaveBeenCalled());
  });

  test('opens backend dialog and performs start/stop actions', async () => {
    const user = userEvent.setup();
    render(<App />);

    await user.click(screen.getByRole('button', { name: 'Backend' }));
    expect(screen.getByRole('heading', { name: 'Backend' })).toBeInTheDocument();
    const dialog = screen.getByRole('dialog');

    await user.click(within(dialog).getByRole('button', { name: 'Stop' }));
    await waitFor(() => expect(screen.getAllByText('stopped').length).toBeGreaterThan(0));

    await user.click(within(dialog).getByRole('button', { name: 'Start' }));
    await waitFor(() => expect(screen.getAllByText('running').length).toBeGreaterThan(0));
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
        accounts: [{ accountId: '11', email: 'imported@test.local', provider: 'openai', status: 'active' }],
      });
    const service = makeService({
      importAccount,
      listAccounts,
    });
    render(<App service={service} />);

    await user.click(screen.getAllByRole('button', { name: '+ Add' })[0]);
    expect(screen.getByRole('button', { name: /Import auth\.json/i })).toBeInTheDocument();

    await user.click(screen.getByRole('button', { name: /Import auth\.json/i }));
    const file = new File(['{"email":"imported@test.local","provider":"openai","token":"x"}'], 'auth.json', {
      type: 'application/json',
    });
    await user.upload(screen.getByLabelText(/Drop auth\.json here or click to browse/i), file);
    await user.click(screen.getByRole('button', { name: 'Import' }));

    await waitFor(() =>
      expect(importAccount).toHaveBeenCalledWith({
        email: 'imported@test.local',
        provider: 'openai',
      }),
    );
    await waitFor(() => expect(listAccounts.mock.calls.length).toBeGreaterThanOrEqual(2));
    expect(await screen.findByText('Account added')).toBeInTheDocument();
    expect(document.querySelector('.success-check svg')).toBeInTheDocument();
  });

  test('account detail token status reflects local encrypted storage and auto-refresh', async () => {
    const user = userEvent.setup();
    const listAccounts = vi.fn(async () => ({
      accounts: [{ accountId: 'acc-1', email: 'alice@test.local', provider: 'openai', status: 'active' }],
    }));
    const service = makeService({
      listAccounts,
    });

    render(<App service={service} />);
    await user.click(screen.getByRole('button', { name: /Accounts/i }));
    await waitFor(() => expect(screen.getByRole('button', { name: /alice@test.local/i })).toBeInTheDocument());
    expect(screen.queryByRole('button', { name: /Latency\s*—/i })).not.toBeInTheDocument();
    await user.click(screen.getByRole('button', { name: /alice@test.local/i }));

    expect(screen.getAllByText('stored encrypted locally').length).toBeGreaterThan(0);
    expect(screen.getByText('stored encrypted locally (auto-refresh on 401)')).toBeInTheDocument();
    expect(screen.queryByText(/expires in \d+m/i)).not.toBeInTheDocument();
    const requestUsageCard = screen.getByRole('heading', { name: 'Request usage' }).closest('.detail-card');
    expect(requestUsageCard).not.toBeNull();
    const usageCard = requestUsageCard as HTMLElement;
    expect(within(usageCard).getByText('Requests')).toBeInTheDocument();
    expect(within(usageCard).getByText('Tokens')).toBeInTheDocument();
    expect(within(usageCard).getByText('Cost')).toBeInTheDocument();
    expect(within(usageCard).getByText('Failovers')).toBeInTheDocument();
    expect(within(usageCard).getByText('$0.00')).toBeInTheDocument();
    expect(within(usageCard).getAllByText('0').length).toBeGreaterThanOrEqual(3);
  });

  test('renders account usage windows by plan from tracked telemetry', async () => {
    const user = userEvent.setup();
    const nowMs = Date.now();
    const listAccounts = vi.fn(async () => ({
      accounts: [
        {
          accountId: 'acc-free',
          email: 'free@test.local',
          provider: 'openai',
          status: 'active',
          planType: 'free',
          quotaPrimaryPercent: 41,
          quotaPrimaryWindowSeconds: 604800,
          quotaPrimaryResetAtMs: nowMs + 3600_000,
        },
        {
          accountId: 'acc-plus',
          email: 'plus@test.local',
          provider: 'openai',
          status: 'active',
          planType: 'plus',
          quotaPrimaryPercent: 22,
          quotaPrimaryWindowSeconds: 18000,
          quotaPrimaryResetAtMs: nowMs + 20 * 60_000,
          quotaSecondaryPercent: 63,
          quotaSecondaryWindowSeconds: 604800,
          quotaSecondaryResetAtMs: nowMs + 2 * 24 * 3600_000,
        },
      ],
    }));
    const service = makeService({
      listAccounts,
    });

    render(<App service={service} />);
    await user.click(screen.getByRole('button', { name: /Accounts/i }));

    const freeAccount = await screen.findByRole('button', { name: /free@test\.local/i });
    expect(freeAccount).toHaveTextContent(/free/i);
    await user.click(freeAccount);
    expect(screen.getByText('Weekly remaining')).toBeInTheDocument();
    expect(screen.queryByText('5h remaining')).not.toBeInTheDocument();

    await user.click(screen.getByRole('button', { name: /plus@test\.local/i }));
    expect(screen.getByText('5h remaining')).toBeInTheDocument();
    expect(screen.getByText('Weekly remaining')).toBeInTheDocument();
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
    const refreshAccountUsageTelemetry = vi.fn(async (accountId: string) => ({
      accountId,
      email: 'alice@test.local',
      provider: 'openai',
      status: 'active',
      planType: 'plus',
      quotaPrimaryPercent: 20,
      quotaSecondaryPercent: 35,
    }));
    const listAccounts = vi
      .fn()
      .mockResolvedValueOnce({ accounts: [] })
      .mockResolvedValueOnce({
        accounts: [{ accountId: '12', email: 'alice@test.local', provider: 'openai', status: 'active' }],
      });
    const service = makeService({
      oauthStart,
      oauthManualCallback,
      refreshAccountUsageTelemetry,
      listAccounts,
    });
    const openSpy = vi.spyOn(window, 'open').mockImplementation(() => null);

    render(<App service={service} />);
    await user.click(screen.getAllByRole('button', { name: '+ Add' })[0]);
    await user.click(screen.getByRole('button', { name: /Browser sign-in/i }));
    await user.click(screen.getByRole('button', { name: 'Open sign-in page' }));

    const callbackUrl = 'http://localhost:1455/auth/callback?code=abc123&state=state-unit&email=alice@test.local';
    await user.type(screen.getByPlaceholderText('http://127.0.0.1:1455/auth/callback?code=...'), callbackUrl);
    await user.click(screen.getByRole('button', { name: 'Submit' }));

    await waitFor(() => expect(oauthStart).toHaveBeenCalledWith({ forceMethod: 'browser' }));
    await waitFor(() => expect(oauthManualCallback).toHaveBeenCalledWith(callbackUrl));
    await waitFor(() => expect(listAccounts.mock.calls.length).toBeGreaterThanOrEqual(2));
    await waitFor(() => expect(refreshAccountUsageTelemetry).toHaveBeenCalledWith('12'));
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
        callbackUrl: 'http://localhost:1455/auth/callback?code=ok&state=state-auto&email=popup@test.local',
        authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=state-auto',
      })
      .mockResolvedValue({
        status: 'success',
        errorMessage: null,
        listenerRunning: false,
        callbackUrl: 'http://localhost:1455/auth/callback?code=ok&state=state-auto&email=popup@test.local',
        authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=state-auto',
      });
    const listAccounts = vi
      .fn()
      .mockResolvedValueOnce({ accounts: [] })
      .mockResolvedValueOnce({
        accounts: [{ accountId: '14', email: 'popup@test.local', provider: 'openai', status: 'active' }],
      });
    const oauthManualCallback = vi.fn(async () => ({ status: 'success', errorMessage: null }));
    const importAccount = vi.fn(async ({ email, provider }: { email: string; provider: string }) => ({
      accountId: '14',
      email,
      provider,
      status: 'active',
    }));
    const service = makeService({
      oauthStart,
      oauthStatus,
      oauthManualCallback,
      importAccount,
      listAccounts,
    });
    const openSpy = vi.spyOn(window, 'open').mockImplementation(() => null);

    render(<App service={service} />);
    await user.click(screen.getAllByRole('button', { name: '+ Add' })[0]);
    await user.click(screen.getByRole('button', { name: /Browser sign-in/i }));
    await user.click(screen.getByRole('button', { name: 'Open sign-in page' }));

    await waitFor(() => expect(oauthStart).toHaveBeenCalledWith({ forceMethod: 'browser' }));
    await waitFor(() => expect(oauthStatus.mock.calls.length).toBeGreaterThanOrEqual(2));
    await waitFor(() => expect(listAccounts.mock.calls.length).toBeGreaterThanOrEqual(2));
    expect(oauthManualCallback).not.toHaveBeenCalled();
    expect(importAccount).not.toHaveBeenCalled();
    expect(openSpy).toHaveBeenCalled();
    expect(await screen.findByText('Account added')).toBeInTheDocument();
    openSpy.mockRestore();
  });

  test('handles oauth deep-link success event and refreshes accounts immediately', async () => {
    const user = userEvent.setup();
    let deepLinkListener: ((event: { kind: 'success' | 'callback'; url: string }) => void) | null = null;
    let deepLinkSuccessTriggered = false;

    const onOauthDeepLink = vi.fn((listener: (event: { kind: 'success' | 'callback'; url: string }) => void) => {
      deepLinkListener = listener;
      return () => {
        if (deepLinkListener === listener) {
          deepLinkListener = null;
        }
      };
    });
    const oauthStart = vi.fn(async () => ({
      method: 'browser',
      authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=state-deep-link',
      callbackUrl: 'http://localhost:1455/auth/callback',
      verificationUrl: null,
      userCode: null,
      deviceAuthId: null,
      intervalSeconds: null,
      expiresInSeconds: null,
    }));
    const oauthStatus = vi.fn(async () => {
      if (deepLinkSuccessTriggered) {
        return {
          status: 'success',
          errorMessage: null,
          listenerRunning: false,
          callbackUrl: 'http://localhost:1455/auth/callback?code=ok&state=state-deep-link&email=deep-link@test.local',
          authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=state-deep-link',
        };
      }
      return {
        status: 'pending',
        errorMessage: null,
        listenerRunning: true,
        callbackUrl: 'http://localhost:1455/auth/callback',
        authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=state-deep-link',
      };
    });
    const listAccounts = vi
      .fn()
      .mockResolvedValueOnce({ accounts: [] })
      .mockResolvedValueOnce({
        accounts: [{ accountId: '15', email: 'deep-link@test.local', provider: 'openai', status: 'active' }],
      });
    const service = makeService({
      onOauthDeepLink,
      oauthStart,
      oauthStatus,
      listAccounts,
    });
    const openSpy = vi.spyOn(window, 'open').mockImplementation(() => null);

    render(<App service={service} />);
    await user.click(screen.getAllByRole('button', { name: '+ Add' })[0]);
    await user.click(screen.getByRole('button', { name: /Browser sign-in/i }));
    await user.click(screen.getByRole('button', { name: 'Open sign-in page' }));

    await waitFor(() => expect(onOauthDeepLink).toHaveBeenCalledTimes(1));
    expect(deepLinkListener).not.toBeNull();

    deepLinkSuccessTriggered = true;
    act(() => {
      deepLinkListener?.({ kind: 'success', url: 'tightrope://oauth/success' });
    });

    await waitFor(() => expect(listAccounts).toHaveBeenCalledTimes(2));
    expect(openSpy).toHaveBeenCalled();
    await waitFor(() =>
      expect(screen.queryByRole('button', { name: 'Open sign-in page' })).not.toBeInTheDocument(),
    );
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
    const service = makeService({
      oauthStart,
      oauthStatus,
    });
    Object.defineProperty(navigator, 'clipboard', {
      configurable: true,
      value: { writeText },
    });

    render(<App service={service} />);
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
    const service = makeService({
      oauthStatus,
      oauthStart,
    });

    render(<App service={service} />);

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

    await waitFor(() => expect(within(screen.getByRole('status')).getByText('backend stopped')).toBeInTheDocument());
  });

  test('wires routing options settings to native settings API', async () => {
    const user = userEvent.setup();
    const settings: DashboardSettings = {
      theme: 'auto',
      stickyThreadsEnabled: false,
      upstreamStreamTransport: 'default',
      preferEarlierResetAccounts: false,
      routingStrategy: 'weighted_round_robin',
      strictLockPoolContinuations: false,
      lockedRoutingAccountIds: [],
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
      routingPlanModelPricingUsdPerMillion: '',
      syncClusterName: 'default',
      syncSiteId: 1,
      syncPort: 9400,
      syncDiscoveryEnabled: true,
      syncIntervalSeconds: 5,
      syncConflictResolution: 'lww',
      syncJournalRetentionDays: 30,
      syncTlsEnabled: true,
      syncRequireHandshakeAuth: true,
      syncClusterSharedSecret: '',
      syncTlsVerifyPeer: true,
      syncTlsCaCertificatePath: '',
      syncTlsCertificateChainPath: '',
      syncTlsPrivateKeyPath: '',
      syncTlsPinnedPeerCertificateSha256: '',
      syncSchemaVersion: 1,
      syncMinSupportedSchemaVersion: 1,
      syncAllowSchemaDowngrade: false,
      syncPeerProbeEnabled: true,
      syncPeerProbeIntervalMs: 5000,
      syncPeerProbeTimeoutMs: 500,
      syncPeerProbeMaxPerRefresh: 2,
      syncPeerProbeFailClosed: true,
      syncPeerProbeFailClosedFailures: 3,
    };
    const updateSettings = vi.fn(async (update: Partial<DashboardSettings>) => ({ ...settings, ...update }));
    const getSettings = vi.fn(async () => ({ ...settings }));
    const service = makeService({
      getSettings,
      updateSettings,
    });

    render(<App service={service} />);
    await user.click(screen.getByRole('button', { name: /Settings/i }));

    const transportRow = screen.getByText('Upstream stream transport').closest('.setting-row');
    expect(transportRow).not.toBeNull();
    await user.selectOptions(within(transportRow as HTMLElement).getByRole('combobox'), 'websocket');
    await user.click(screen.getByRole('button', { name: 'Toggle sticky threads' }));
    await user.click(screen.getByRole('button', { name: 'Toggle prefer earlier reset' }));
    expect(updateSettings).not.toHaveBeenCalled();

    await user.click(screen.getByRole('button', { name: 'Save' }));

    expect(getSettings).toHaveBeenCalled();
    expect(updateSettings).toHaveBeenCalled();
    expect(updateSettings.mock.calls.some(([payload]) => payload?.upstreamStreamTransport === 'websocket')).toBe(true);
    expect(updateSettings.mock.calls.some(([payload]) => payload?.stickyThreadsEnabled === true)).toBe(true);
    expect(updateSettings.mock.calls.some(([payload]) => payload?.preferEarlierResetAccounts === true)).toBe(true);
  });

  test('keeps sync settings stable when cluster status payload is partial', async () => {
    const user = userEvent.setup();
    const getClusterStatus = vi.fn(async () =>
      ({
        enabled: false,
        site_id: '1',
        cluster_name: 'alpha',
        role: 'standalone',
        term: 1,
        commit_index: 4,
        leader_id: null,
        peers: [
          {
            site_id: '2',
            address: '10.0.0.2:9400',
          },
        ],
      }) as any,
    );
    const service = makeService({
      getClusterStatus,
    });

    render(<App service={service} />);
    await user.click(screen.getByRole('button', { name: /Settings/i }));

    expect(screen.getByRole('heading', { name: 'Database synchronization' })).toBeInTheDocument();
    expect(screen.getByText('10.0.0.2:9400')).toBeInTheDocument();
    expect(screen.getByText(/disconnected · lag 0/i)).toBeInTheDocument();
  });

  test('prompts to save or discard when leaving settings with unsaved changes', async () => {
    const user = userEvent.setup();
    render(<App />);

    await user.click(screen.getByRole('button', { name: /Settings/i }));
    await user.click(screen.getByRole('button', { name: 'Toggle sticky threads' }));
    await user.click(screen.getByRole('button', { name: /Logs/i }));

    const leaveDialog = screen.getByRole('dialog');
    expect(within(leaveDialog).getByRole('heading', { name: 'Unsaved Settings' })).toBeInTheDocument();
    expect(screen.getByRole('heading', { name: 'Settings' })).toBeInTheDocument();

    await user.click(within(leaveDialog).getByRole('button', { name: 'Discard' }));
    expect(screen.getByRole('heading', { name: 'Logs' })).toBeInTheDocument();
  });

  test('wires firewall and sync settings controls to native bridge', async () => {
    const user = userEvent.setup();
    const listFirewallIps = vi
      .fn()
      .mockResolvedValueOnce({ mode: 'allow_all', entries: [] })
      .mockResolvedValue({ mode: 'allowlist_active', entries: [{ ipAddress: '192.168.0.1', createdAt: 'now' }] });
    const addFirewallIp = vi.fn(async () => ({ ipAddress: '192.168.0.1', createdAt: 'now' }));
    const clusterEnable = vi.fn(async (_config: unknown) => {});
    const addPeer = vi.fn(async () => {});
    const triggerSync = vi.fn(async () => {});
    const service = makeService({
      listFirewallIps,
      addFirewallIp,
      clusterEnable,
      addPeer,
      triggerSync,
    });

    render(<App service={service} />);
    await user.click(screen.getByRole('button', { name: /Settings/i }));

    await user.type(screen.getByPlaceholderText('192.168.0.1'), '192.168.0.1');
    await user.click(screen.getByRole('button', { name: 'Add' }));
    expect(addFirewallIp).toHaveBeenCalledWith('192.168.0.1');

    await user.click(screen.getByRole('button', { name: 'Toggle sync' }));
    expect(clusterEnable).toHaveBeenCalled();

    await user.type(screen.getByPlaceholderText('host:port'), '10.0.0.5:9400');
    await user.click(screen.getByRole('button', { name: 'Add peer' }));
    expect(addPeer).toHaveBeenCalledWith('10.0.0.5:9400');

    await user.click(screen.getByRole('button', { name: 'Trigger sync now' }));
    expect(triggerSync).toHaveBeenCalled();
  });

  test('falls back to discovery disabled when sync enable rejects localhost discovery host', async () => {
    const user = userEvent.setup();
    const settings: DashboardSettings = {
      theme: 'auto',
      stickyThreadsEnabled: false,
      upstreamStreamTransport: 'default',
      preferEarlierResetAccounts: false,
      routingStrategy: 'weighted_round_robin',
      strictLockPoolContinuations: false,
      lockedRoutingAccountIds: [],
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
      routingPlanModelPricingUsdPerMillion: '',
      syncClusterName: 'default',
      syncSiteId: 1,
      syncPort: 9400,
      syncDiscoveryEnabled: true,
      syncIntervalSeconds: 5,
      syncConflictResolution: 'lww',
      syncJournalRetentionDays: 30,
      syncTlsEnabled: true,
      syncRequireHandshakeAuth: true,
      syncClusterSharedSecret: '',
      syncTlsVerifyPeer: true,
      syncTlsCaCertificatePath: '',
      syncTlsCertificateChainPath: '',
      syncTlsPrivateKeyPath: '',
      syncTlsPinnedPeerCertificateSha256: '',
      syncSchemaVersion: 1,
      syncMinSupportedSchemaVersion: 1,
      syncAllowSchemaDowngrade: false,
      syncPeerProbeEnabled: true,
      syncPeerProbeIntervalMs: 5000,
      syncPeerProbeTimeoutMs: 500,
      syncPeerProbeMaxPerRefresh: 2,
      syncPeerProbeFailClosed: true,
      syncPeerProbeFailClosedFailures: 3,
    };
    const getSettings = vi.fn(async () => ({ ...settings }));
    const updateSettings = vi.fn(async (update: Partial<DashboardSettings>) => ({ ...settings, ...update }));
    const clusterEnable = vi.fn(async (config: unknown) => {
      const discoveryEnabled = (
        config && typeof config === 'object' ? (config as { discovery_enabled?: boolean }).discovery_enabled : undefined
      ) !== false;
      if (discoveryEnabled) {
        throw new Error(
          'clusterEnable failed: cluster discovery requires a routable host; set TIGHTROPE_HOST or TIGHTROPE_CONNECT_ADDRESS',
        );
      }
    });
    const service = makeService({
      getSettings,
      updateSettings,
      clusterEnable,
    });

    render(<App service={service} />);
    await user.click(screen.getByRole('button', { name: /Settings/i }));
    await user.click(screen.getByRole('button', { name: 'Toggle sync' }));

    await waitFor(() => expect(clusterEnable).toHaveBeenCalledTimes(2));
    expect(clusterEnable.mock.calls[0][0]).toMatchObject({ discovery_enabled: true });
    expect(clusterEnable.mock.calls[1][0]).toMatchObject({ discovery_enabled: false });
    expect(updateSettings.mock.calls.some(([payload]) => payload?.syncDiscoveryEnabled === false)).toBe(true);
  });

  test('wires account detail actions to native account APIs', async () => {
    const user = userEvent.setup();
    const listAccounts = vi
      .fn()
      .mockResolvedValueOnce({
        accounts: [{ accountId: '10', email: 'ops@test.local', provider: 'openai', status: 'active' }],
      })
      .mockResolvedValueOnce({
        accounts: [
          {
            accountId: '10',
            email: 'ops@test.local',
            provider: 'openai',
            status: 'active',
            quotaPrimaryPercent: 22,
            quotaSecondaryPercent: 61,
          },
        ],
      })
      .mockResolvedValueOnce({
        accounts: [{ accountId: '10', email: 'ops@test.local', provider: 'openai', status: 'paused' }],
      })
      .mockResolvedValueOnce({
        accounts: [{ accountId: '10', email: 'ops@test.local', provider: 'openai', status: 'active' }],
      })
      .mockResolvedValueOnce({
        accounts: [],
      });
    const pauseAccount = vi.fn(async () => ({ status: 'paused' }));
    const reactivateAccount = vi.fn(async () => ({ status: 'reactivated' }));
    const deleteAccount = vi.fn(async () => ({ status: 'deleted' }));
    const refreshAccountUsageTelemetry = vi.fn(async () => ({
      accountId: '10',
      email: 'ops@test.local',
      provider: 'openai',
      status: 'active',
      quotaPrimaryPercent: 22,
      quotaSecondaryPercent: 61,
    }));
    const service = makeService({
      listAccounts,
      pauseAccount,
      reactivateAccount,
      deleteAccount,
      refreshAccountUsageTelemetry,
    });

    render(<App service={service} />);
    await user.click(screen.getByRole('button', { name: /Accounts/i }));

    const accountRow = await screen.findByRole('button', { name: /ops@test\.local/i });
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
    const service = makeService({
      oauthStatusRequest: oauthStatus,
      oauthStartRequest: oauthStart,
      oauthStopRequest: oauthStop,
      oauthRestartRequest: oauthRestart,
    });

    render(<App service={service} />);

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
