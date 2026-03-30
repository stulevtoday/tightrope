import { expect, test } from '@playwright/test';

test.describe('tightrope renderer', () => {
  test('navigates pages and opens logs drawer', async ({ page }) => {
    await page.goto('/');

    await expect(page.getByRole('heading', { name: 'Routing pool' })).toBeVisible();
    await page.getByRole('button', { name: 'Logs' }).click();
    await expect(page.getByRole('heading', { name: 'Logs' })).toBeVisible();

    const firstLogRow = page.locator('#pageLogs tbody tr').first();
    await firstLogRow.click();
    await expect(page.locator('.log-drawer.open')).toBeVisible();

    await page.locator('.log-drawer.open .dialog-close').click();
    await expect(page.locator('.log-drawer.open')).toHaveCount(0);
  });

  test('controls oauth listener lifecycle from main oauth dialog', async ({ page }) => {
    let startCalls = 0;
    let stopCalls = 0;
    let restartCalls = 0;
    const accounts: Array<{ accountId: string; email: string; provider: string; status: string }> = [];

    await page.route('**/api/oauth/status', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          status: 'pending',
          errorMessage: null,
          listenerRunning: false,
          callbackUrl: 'http://localhost:1455/auth/callback',
          authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=initial',
        }),
      });
    });

    await page.route('**/api/oauth/start', async (route) => {
      startCalls += 1;
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          method: 'browser',
          authorizationUrl: `https://auth.openai.com/oauth/authorize?response_type=code&state=start-${startCalls}`,
          callbackUrl: 'http://localhost:1455/auth/callback',
          verificationUrl: null,
          userCode: null,
          deviceAuthId: null,
          intervalSeconds: null,
          expiresInSeconds: null,
        }),
      });
    });

    await page.route('**/api/oauth/stop', async (route) => {
      stopCalls += 1;
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          status: 'stopped',
          errorMessage: null,
          listenerRunning: false,
          callbackUrl: 'http://localhost:1455/auth/callback',
          authorizationUrl: 'https://auth.openai.com/oauth/authorize?response_type=code&state=stopped',
        }),
      });
    });

    await page.route('**/api/oauth/restart', async (route) => {
      restartCalls += 1;
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          method: 'browser',
          authorizationUrl: `https://auth.openai.com/oauth/authorize?response_type=code&state=restart-${restartCalls}`,
          callbackUrl: 'http://localhost:1455/auth/callback',
          verificationUrl: null,
          userCode: null,
          deviceAuthId: null,
          intervalSeconds: null,
          expiresInSeconds: null,
        }),
      });
    });

    await page.route('**/api/accounts', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ accounts }),
      });
    });

    await page.route('**/api/accounts/import', async (route) => {
      const payload = await route.request().postDataJSON();
      const email = String(payload?.email ?? '');
      const provider = String(payload?.provider ?? 'openai');
      const account = {
        accountId: String(accounts.length + 1),
        email,
        provider,
        status: 'active',
      };
      accounts.push(account);
      await route.fulfill({
        status: 201,
        contentType: 'application/json',
        body: JSON.stringify(account),
      });
    });

    await page.goto('/');
    await page.getByRole('button', { name: 'OAuth' }).click();
    const dialog = page.getByRole('dialog');

    await dialog.getByRole('button', { name: 'Initialize' }).click();
    await dialog.getByRole('button', { name: /^Start Listener$/ }).click();
    await expect(dialog.getByRole('button', { name: /^Stop Listener$/ })).toBeVisible();

    await dialog.getByRole('button', { name: /^Stop Listener$/ }).click();
    await expect(dialog.getByRole('button', { name: /^Start Listener$/ })).toBeVisible();

    await dialog.getByRole('button', { name: /^Restart Listener$/ }).click();
    await expect(dialog.getByRole('button', { name: /^Stop Listener$/ })).toBeVisible();

    expect(startCalls).toBe(1);
    expect(stopCalls).toBe(1);
    expect(restartCalls).toBe(1);
  });

  test('runs add account browser flow', async ({ page }) => {
    let oauthState = 'state-e2e';
    const accounts: Array<{ accountId: string; email: string; provider: string; status: string }> = [];

    await page.route('**/api/oauth/start', async (route) => {
      oauthState = `state-${Date.now()}`;
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          method: 'browser',
          authorizationUrl: `https://auth.openai.com/oauth/authorize?response_type=code&state=${oauthState}`,
          callbackUrl: 'http://localhost:1455/auth/callback',
          verificationUrl: null,
          userCode: null,
          deviceAuthId: null,
          intervalSeconds: null,
          expiresInSeconds: null,
        }),
      });
    });

    await page.route('**/api/oauth/manual-callback', async (route) => {
      const payload = await route.request().postDataJSON();
      const callbackUrl = String(payload?.callbackUrl ?? '');
      const valid = callbackUrl.includes(`state=${oauthState}`) && callbackUrl.includes('code=');
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          status: valid ? 'success' : 'error',
          errorMessage: valid ? null : 'Invalid OAuth callback: state mismatch or missing code.',
        }),
      });
    });

    await page.route('**/api/oauth/status', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          status: 'success',
          errorMessage: null,
          listenerRunning: false,
          callbackUrl: 'http://localhost:1455/auth/callback',
          authorizationUrl: `https://auth.openai.com/oauth/authorize?response_type=code&state=${oauthState}`,
        }),
      });
    });

    await page.route('**/api/accounts', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ accounts }),
      });
    });

    await page.route('**/api/accounts/import', async (route) => {
      const payload = await route.request().postDataJSON();
      const email = String(payload?.email ?? '');
      const provider = String(payload?.provider ?? 'openai');
      const account = {
        accountId: String(accounts.length + 1),
        email,
        provider,
        status: 'active',
      };
      accounts.push(account);
      await route.fulfill({
        status: 201,
        contentType: 'application/json',
        body: JSON.stringify(account),
      });
    });

    await page.route('**/auth/callback**', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'text/html',
        body: '<html><body>ok</body></html>',
      });
    });

    await page.goto('/');
    await page.getByRole('button', { name: '+ Add' }).click();
    await page.getByRole('button', { name: 'Browser sign-in recommended' }).click();
    await page.getByRole('button', { name: 'Open sign-in page' }).click();
    const manualInput = page.getByPlaceholder('http://127.0.0.1:1455/auth/callback?code=...');
    await manualInput.fill(`http://localhost:1455/auth/callback?code=e2e&state=${oauthState}`);
    await page.getByRole('button', { name: 'Submit' }).click();
    await expect(page.getByText('Account added')).toBeVisible();
  });

  test('runs add account import flow and persists through account API', async ({ page }) => {
    let importCalls = 0;
    const accounts: Array<{ accountId: string; email: string; provider: string; status: string }> = [];

    await page.route('**/api/accounts', async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ accounts }),
      });
    });

    await page.route('**/api/accounts/import', async (route) => {
      importCalls += 1;
      const payload = await route.request().postDataJSON();
      const account = {
        accountId: String(accounts.length + 1),
        email: String(payload?.email ?? ''),
        provider: String(payload?.provider ?? 'openai'),
        status: 'active',
      };
      accounts.push(account);
      await route.fulfill({
        status: 201,
        contentType: 'application/json',
        body: JSON.stringify(account),
      });
    });

    await page.goto('/');
    await page.getByRole('button', { name: '+ Add' }).click();
    await page.getByRole('button', { name: 'Import auth.json' }).click();
    await page.locator('#fileInput').setInputFiles({
      name: 'auth.json',
      mimeType: 'application/json',
      buffer: Buffer.from(JSON.stringify({ email: 'import-e2e@example.com', provider: 'openai', token: 'x' })),
    });
    await page.getByRole('button', { name: 'Import' }).click();

    await expect(page.getByText('Account added')).toBeVisible();
    expect(importCalls).toBe(1);
    expect(accounts).toHaveLength(1);
    expect(accounts[0]?.email).toBe('import-e2e@example.com');
  });
});
