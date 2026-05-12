#!/usr/bin/env node

const { createRequire } = require('node:module');
const { readFile } = require('node:fs/promises');
const { join, resolve } = require('node:path');
const tls = require('node:tls');
const net = require('node:net');

const appRequire = createRequire(join(__dirname, '..', 'app', 'package.json'));
const { chromium } = appRequire('playwright');

const TIGHTROPE_API = 'http://127.0.0.1:2455';

const EMAIL_INPUT_SELECTORS = [
  'input[name="email"]',
  'input[type="email"]',
  'input[id="email-input"]',
  'input[autocomplete="email"]',
  'input[name="identifier"]',
  'input[id="identifierId"]',
];
const PASSWORD_INPUT_SELECTORS = [
  'input[type="password"]',
  'input[name="password"]',
  'input[id="password"]',
];
const CONTINUE_BUTTON_SELECTORS = [
  'button[type="submit"]',
  'button[data-testid="login-button"]',
  'button[name="action"]',
  'button:has-text("Continue")',
  'button:has-text("Next")',
  'button:has-text("Продолжить")',
];
const CONSENT_BUTTON_SELECTORS = [
  'button[name="action"][value="consent"]',
  'button[data-testid="consent-continue"]',
  'button[data-testid="allow-button"]',
  'button:has-text("Continue")',
  'button:has-text("Allow")',
  'button:has-text("Accept")',
  'button:has-text("Продолжить")',
];
const CODE_INPUT_SELECTORS = [
  'input[name="code"]',
  'input[aria-label*="code" i]',
  'input[placeholder*="code" i]',
  'input[type="tel"][maxlength="6"]',
  'input[type="text"][maxlength="6"]',
];
const CAPTCHA_SELECTORS = [
  '[data-testid="captcha"]',
  '.cf-turnstile',
  'iframe[src*="captcha"]',
  'iframe[src*="challenges.cloudflare.com"]',
];

const IMAP_SERVERS = {
  'outlook.com': { host: 'outlook.office365.com', port: 993, ssl: true },
  'hotmail.com': { host: 'outlook.office365.com', port: 993, ssl: true },
  'live.com': { host: 'outlook.office365.com', port: 993, ssl: true },
  'gmail.com': { host: 'imap.gmail.com', port: 993, ssl: true },
  'yahoo.com': { host: 'imap.mail.yahoo.com', port: 993, ssl: true },
  'firstmail.ltd': { host: 'firstmail.ltd', port: 993, ssl: true },
  'legenmail.com': { host: 'firstmail.ltd', port: 993, ssl: true },
};

function getImapServer(email) {
  const domain = email.split('@')[1]?.toLowerCase();
  if (!domain) return null;
  for (const [key, config] of Object.entries(IMAP_SERVERS)) {
    if (domain.endsWith(key)) return config;
  }
  const parts = domain.split('.');
  for (let i = 1; i < parts.length; i++) {
    const sub = parts.slice(i).join('.');
    if (IMAP_SERVERS[sub]) return IMAP_SERVERS[sub];
  }
  return null;
}

function normalizeEmail(email) {
  return typeof email === 'string' ? email.trim().toLowerCase() : '';
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function retry(fn, attempts, delayMs, label) {
  let lastError = null;
  for (let attempt = 1; attempt <= attempts; attempt += 1) {
    try {
      return await fn(attempt);
    } catch (error) {
      lastError = error;
      if (attempt < attempts) {
        console.log(`    ${label} failed (attempt ${attempt}/${attempts}): ${error.message}`);
        await sleep(delayMs);
      }
    }
  }
  throw lastError;
}

async function findVisibleLocator(page, selectors, timeoutMs = 3000) {
  for (const selector of selectors) {
    try {
      const locator = page.locator(selector).first();
      await locator.waitFor({ state: 'visible', timeout: timeoutMs });
      return locator;
    } catch {}
  }
  return null;
}

async function clickFirstVisible(page, selectors, timeoutMs = 3000) {
  const locator = await findVisibleLocator(page, selectors, timeoutMs);
  if (!locator) {
    return false;
  }
  try {
    await locator.click({ timeout: timeoutMs });
    return true;
  } catch {
    try {
      await locator.click({ timeout: timeoutMs, force: true });
      return true;
    } catch {}
  }
  return false;
}

async function fillFirstVisible(page, selectors, value, timeoutMs = 3000) {
  const locator = await findVisibleLocator(page, selectors, timeoutMs);
  if (!locator) {
    return false;
  }
  try {
    await locator.fill(value, { timeout: timeoutMs });
    return true;
  } catch {
    try {
      await locator.click({ timeout: timeoutMs, force: true });
      await locator.fill(value, { timeout: timeoutMs, force: true });
      return true;
    } catch {}
  }
  return false;
}

async function pressEnterOnFirstVisible(page, selectors, timeoutMs = 3000) {
  const locator = await findVisibleLocator(page, selectors, timeoutMs);
  if (!locator) {
    return false;
  }
  try {
    await locator.press('Enter', { timeout: timeoutMs });
    return true;
  } catch {
    try {
      await locator.click({ timeout: timeoutMs, force: true });
      await locator.press('Enter', { timeout: timeoutMs });
      return true;
    } catch {}
  }
  return false;
}

async function detectPageState(page) {
  const url = page.url();
  if (url.includes('localhost:1455/auth/callback') || url.includes('tightrope://')) {
    return 'callback';
  }
  if (await findVisibleLocator(page, CODE_INPUT_SELECTORS, 1000)) {
    return 'code';
  }
  if (await findVisibleLocator(page, PASSWORD_INPUT_SELECTORS, 1000)) {
    return 'password';
  }
  if (await findVisibleLocator(page, EMAIL_INPUT_SELECTORS, 1000)) {
    return 'email';
  }
  if (await findVisibleLocator(page, CONSENT_BUTTON_SELECTORS, 1000)) {
    return 'consent';
  }
  return 'unknown';
}

class ImapClient {
  constructor(host, port, useSsl = true) {
    this.host = host;
    this.port = port;
    this.useSsl = useSsl;
    this.socket = null;
    this.tagCounter = 0;
    this.pending = [];
    this.buffer = '';
    this.connected = false;
  }

  connect() {
    return new Promise((resolve, reject) => {
      const connectTimeout = setTimeout(() => {
        reject(new Error(`IMAP connection timeout to ${this.host}:${this.port}`));
        try { this.socket?.destroy(); } catch {}
      }, 15000);

      const connectOptions = {
        host: this.host,
        port: this.port,
        rejectUnauthorized: false,
      };

      const onConnect = () => {
        clearTimeout(connectTimeout);
        this.connected = true;
      };

      if (this.useSsl) {
        this.socket = tls.connect(connectOptions, onConnect);
      } else {
        this.socket = net.connect(connectOptions, onConnect);
      }

      this.socket.once('error', reject);

      this.socket.on('data', (data) => {
        this.buffer += data.toString();
        this.processBuffer();
      });

      this.socket.once('close', () => {
        clearTimeout(connectTimeout);
        this.connected = false;
      });

      this.waitUntagged(/^.*/).then((greeting) => {
        clearTimeout(connectTimeout);
        this.socket.removeListener('error', reject);
        resolve(greeting);
      }).catch((err) => {
        clearTimeout(connectTimeout);
        reject(err);
      });
    });
  }

  processBuffer() {
    while (this.buffer.length > 0) {
      if (this.pending.length === 0) break;

      const { resolve, reject, pattern, tag } = this.pending[0];

      if (tag) {
        const tagIdx = this.buffer.indexOf(tag);
        if (tagIdx !== -1) {
          const afterTag = this.buffer.indexOf('\r\n', tagIdx);
          if (afterTag !== -1) {
            const response = this.buffer.substring(0, afterTag + 2);
            this.buffer = this.buffer.substring(afterTag + 2);
            this.pending.shift();
            resolve(response);
            continue;
          }
        }
      } else {
        const match = this.buffer.match(pattern);
        if (match) {
          const endOfLine = this.buffer.indexOf('\r\n', match.index);
          if (endOfLine !== -1) {
            const response = this.buffer.substring(0, endOfLine + 2);
            this.buffer = this.buffer.substring(endOfLine + 2);
            this.pending.shift();
            resolve(response);
            continue;
          }
        }
      }
      break;
    }
  }

  waitUntagged(pattern) {
    return new Promise((resolve, reject) => {
      this.pending.push({ resolve, reject, pattern, tag: null });
      this.processBuffer();
    });
  }

  sendCommand(command) {
    const tag = `A${String(this.tagCounter++).padStart(3, '0')}`;
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        reject(new Error(`IMAP command timeout: ${tag} ${command}`));
      }, 30000);

      this.pending.push({
        resolve: (data) => {
          clearTimeout(timeout);
          resolve(data);
        },
        reject: (err) => {
          clearTimeout(timeout);
          reject(err);
        },
        pattern: null,
        tag,
      });

      this.socket.write(`${tag} ${command}\r\n`);
    });
  }

  async login(user, password) {
    return this.sendCommand(`LOGIN "${user}" "${password}"`);
  }

  async select(mailbox = 'INBOX') {
    return this.sendCommand(`SELECT "${mailbox}"`);
  }

  async searchRecent(sinceMinutes = 30) {
    const since = new Date(Date.now() - sinceMinutes * 60 * 1000);
    const dateStr = since.toLocaleDateString('en-US', {
      day: '2-digit', month: 'short', year: 'numeric'
    }).replace(',', '');
    return this.sendCommand(`SEARCH SINCE "${dateStr}"`);
  }

  async fetchBody(seqNum) {
    return this.sendCommand(`FETCH ${seqNum} (BODY[TEXT])`);
  }

  async fetchHeader(seqNum) {
    return this.sendCommand(`FETCH ${seqNum} (BODY[HEADER.FIELDS (SUBJECT FROM DATE)])`);
  }

  async fetchEnvelope(seqNum) {
    return this.sendCommand(`FETCH ${seqNum} (ENVELOPE)`);
  }

  disconnect() {
    if (this.socket) {
      try { this.socket.write('A999 LOGOUT\r\n'); } catch {}
      this.socket.destroy();
      this.connected = false;
    }
  }
}

function extractVerificationCode(text) {
  const patterns = [
    /\b(\d{6})\b/g,
    /code[:\s]+(\d{4,8})/i,
    /verification[:\s]+(\d{4,8})/i,
    /verify[:\s]+(\d{4,8})/i,
    /OTP[:\s]+(\d{4,8})/i,
    /(\d{6})\s+is\s+your/i,
  ];

  for (const pattern of patterns) {
    const match = text.match(pattern);
    if (match) return match[1];
  }
  return null;
}

async function getCodeFromImap(email, password, keywords = [], maxWaitMs = 120000) {
  const server = getImapServer(email);
  if (!server) {
    console.log(`    IMAP: No server config for ${email}`);
    return null;
  }

  const start = Date.now();
  const pollInterval = 5000;

  while (Date.now() - start < maxWaitMs) {
    const imap = new ImapClient(server.host, server.port, server.ssl);
    try {
      await imap.connect();
      await imap.login(email, password);

      const selectResult = await imap.select('INBOX');

      const searchResult = await imap.searchRecent(10);
      const seqNums = searchResult.match(/\d+/g);
      if (!seqNums || seqNums.length === 0) {
        imap.disconnect();
        await new Promise(r => setTimeout(r, pollInterval));
        continue;
      }

      for (const seqNum of seqNums.reverse()) {
        const header = await imap.fetchHeader(seqNum);
        const body = await imap.fetchBody(seqNum);
        const fullText = `${header}\n${body}`;

        const isRelevant = keywords.some(kw => fullText.toLowerCase().includes(kw.toLowerCase()));
        if (!isRelevant) continue;

        const code = extractVerificationCode(fullText);
        if (code) {
          imap.disconnect();
          return code;
        }
      }

      imap.disconnect();
    } catch (err) {
      console.log(`    IMAP error for ${email}: ${err.message}`);
      try { imap.disconnect(); } catch {}
    }

    await new Promise(r => setTimeout(r, pollInterval));
  }

  return null;
}

function parseAccountLine(line) {
  const trimmed = line.trim();
  if (!trimmed || trimmed.startsWith('#')) return null;
  const parts = trimmed.split(':');
  if (parts.length < 2) return null;
  const email = normalizeEmail(parts[0]);
  const backupEmail = normalizeEmail(parts[3] || '');
  if (!email) {
    return null;
  }
  return {
    email,
    password: parts[1].trim(),
    emailPassword: parts[2]?.trim() || parts[1].trim(),
    backupEmail,
    backupEmailPassword: parts[4]?.trim() || '',
  };
}

async function startOAuthFlow() {
  const res = await fetch(`${TIGHTROPE_API}/api/oauth/start`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ forceMethod: 'browser' }),
  });
  if (!res.ok) {
    const text = await res.text();
    throw new Error(`OAuth start failed (${res.status}): ${text}`);
  }
  return res.json();
}

async function stopOAuthFlow() {
  try {
    await fetch(`${TIGHTROPE_API}/api/oauth/stop`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: '{}',
    });
  } catch {}
}

async function getOAuthStatus() {
  try {
    const res = await fetch(`${TIGHTROPE_API}/api/oauth/status`);
    if (!res.ok) return { status: 'unknown' };
    return res.json();
  } catch {
    return { status: 'unknown' };
  }
}

async function listAccounts() {
  const res = await fetch(`${TIGHTROPE_API}/api/accounts`);
  if (!res.ok) throw new Error(`Failed to list accounts: ${res.status}`);
  const data = await res.json();
  return data.accounts || data || [];
}

async function getExistingAccountsByEmail() {
  const accounts = await listAccounts();
  const map = new Map();
  for (const account of accounts) {
    const email = normalizeEmail(account?.email);
    if (!email) {
      continue;
    }
    if (!map.has(email)) {
      map.set(email, account);
    }
  }
  return map;
}

async function waitForAccountAdded(existingEmails, timeoutMs = 60000) {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    const accounts = await listAccounts();
    const currentEmails = new Set(accounts.map((a) => normalizeEmail(a?.email)).filter(Boolean));
    for (const email of currentEmails) {
      if (!existingEmails.has(email)) {
        return accounts.find((a) => normalizeEmail(a?.email) === email);
      }
    }
    await new Promise((r) => setTimeout(r, 1000));
  }
  return null;
}

async function resolveVerificationCode(account, source, keywords, maxWaitMs = 120000) {
  console.log(`    Checking ${source} for code (keywords: ${keywords.join(', ')})...`);

  if (source === 'email') {
    return getCodeFromImap(account.email, account.emailPassword, keywords, maxWaitMs);
  }
  if (source === 'backup' && account.backupEmail && account.backupEmailPassword) {
    return getCodeFromImap(account.backupEmail, account.backupEmailPassword, keywords, maxWaitMs);
  }
  return null;
}

async function addAccount(account, browser, options) {
  const { headless, knownEmails } = options;
  console.log(`\n=== Adding: ${account.email} ===`);

  const existingAccounts = await getExistingAccountsByEmail();
  const existingEmailSet = new Set(existingAccounts.keys());
  for (const email of existingAccounts.keys()) {
    if (knownEmails instanceof Set) {
      knownEmails.add(email);
    }
  }
  if (existingAccounts.has(account.email)) {
    console.log(`  SKIP: ${account.email} already exists`);
    return { email: account.email, status: 'skipped' };
  }

  await stopOAuthFlow();
  await new Promise((r) => setTimeout(r, 500));

  const oauthData = await retry(() => startOAuthFlow(), 3, 1500, 'OAuth start');
  if (!oauthData.authorizationUrl) {
    console.log(`  ERROR: No authorization URL received`);
    return { email: account.email, status: 'error' };
  }

  console.log('  Opening auth URL...');
  const context = await browser.newContext();
  const page = await context.newPage();
  page.setDefaultTimeout(12000);
  page.setDefaultNavigationTimeout(30000);

  try {
    await retry(
      async () => {
        try {
          await page.goto(oauthData.authorizationUrl, { waitUntil: 'domcontentloaded', timeout: 30000 });
        } catch (error) {
          console.log(`  NAV WARNING: ${error.message}`);
          throw error;
        }
        await page.waitForTimeout(1200);
        return undefined;
      },
      2,
      1000,
      'Auth page navigation'
    );

    await retry(
      async (attempt) => {
        if (attempt > 1) {
          try {
            await page.reload({ waitUntil: 'domcontentloaded', timeout: 30000 });
            await sleep(800);
          } catch {}
        }
        if (!(await fillFirstVisible(page, EMAIL_INPUT_SELECTORS, account.email, 5000))) {
          throw new Error('Could not find email input');
        }
        return true;
      },
      3,
      1000,
      'Email field'
    );
    await sleep(300);
    if (!(await clickFirstVisible(page, CONTINUE_BUTTON_SELECTORS, 4000))) {
      await pressEnterOnFirstVisible(page, EMAIL_INPUT_SELECTORS, 2000);
    }
    console.log(`  Email entered: ${account.email}`);

    console.log('  Waiting for password field...');
    let passwordInput = null;
    for (let i = 0; i < 40; i++) {
      passwordInput = await findVisibleLocator(page, PASSWORD_INPUT_SELECTORS, 1000);
      if (passwordInput) break;
      if ((await detectPageState(page)) === 'callback') {
        console.log('  OAuth callback received before password step');
        break;
      }
      const oauthStatus = await getOAuthStatus();
      if (oauthStatus.status === 'success') break;
      await sleep(1000);
    }

    if (passwordInput) {
      console.log('  Password field found, entering password...');
      try {
        await passwordInput.click({ timeout: 3000 });
        await passwordInput.fill(account.password);
        await sleep(300);
        if (!(await clickFirstVisible(page, CONTINUE_BUTTON_SELECTORS, 2500))) {
          await passwordInput.press('Enter');
        }
        console.log('  Password submitted');
      } catch (err) {
        console.log(`  Password entry error: ${err.message}`);
      }
      await sleep(2500);
    }

    console.log('  Checking for consent/continue page...');
    for (let i = 0; i < 10; i++) {
      if ((await detectPageState(page)) === 'callback') {
        console.log('  Callback received during consent check');
        break;
      }
      const oauthStatus = await getOAuthStatus();
      if (oauthStatus.status === 'success') {
        console.log('  OAuth status: success (during consent check)');
        break;
      }

      if (await clickFirstVisible(page, CONSENT_BUTTON_SELECTORS, 3000)) {
        console.log('  Consent/Continue page found, clicking...');
        await sleep(2000);
        break;
      }

      const codeInput = await findVisibleLocator(page, CODE_INPUT_SELECTORS, 1000);
      const passwordAgain = await findVisibleLocator(page, PASSWORD_INPUT_SELECTORS, 1000);
      if (codeInput || passwordAgain) break;

      await sleep(1000);
    }

    console.log('  Waiting for OAuth callback or code request...');
    const maxAuthTime = options.manualTimeout || 240000;
    const authStart = Date.now();
    let lastCodeAttempt = 0;
    let codeCheckSource = null;

    while (Date.now() - authStart < maxAuthTime) {
      let url;
      try {
        url = page.url();
      } catch {
        console.log('  Page context destroyed - checking OAuth status...');
        const status = await getOAuthStatus();
        if (status.status === 'success') {
          console.log('  OAuth status: success (page navigated away)');
          break;
        }
        await sleep(2000);
        continue;
      }

      if (url.includes('localhost:1455/auth/callback') || url.includes('tightrope://')) {
        console.log('  Callback received!');
        break;
      }

      let oauthStatus;
      try {
        oauthStatus = await getOAuthStatus();
      } catch (err) {
        console.log(`  OAuth status check error: ${err.message}`);
        await sleep(2000);
        continue;
      }
      if (oauthStatus.status === 'success') {
        console.log('  OAuth status: success');
        break;
      }

      let needsCode, codeHint;
      try {
        needsCode = await findVisibleLocator(page, CODE_INPUT_SELECTORS, 1000);
        codeHint = await page.locator('text=/enter the code|verification code|verify your email|we sent a code|enter the verification/i').first();
        if (codeHint !== null) {
          await codeHint.waitFor({ state: 'visible', timeout: 1000 });
        }
      } catch (err) {
        console.log(`  Page selector error: ${err.message}`);
        await sleep(2000);
        continue;
      }

      if (needsCode || codeHint) {
        const timeSinceLastAttempt = Date.now() - lastCodeAttempt;
        if (timeSinceLastAttempt > 8000) {
          lastCodeAttempt = Date.now();

          if (!codeCheckSource) {
            console.log('  Verification code required, checking emails via IMAP...');
            codeCheckSource = 'trying';
          }

          let code = null;

          code = await resolveVerificationCode(account, 'email', ['openai', 'verify', 'code', 'login', 'security'], 30000);

          if (!code && account.backupEmail && account.backupEmailPassword) {
            console.log('  Code not found in primary email, checking backup/Outlook verification email...');
            code = await resolveVerificationCode(
              { email: account.email, emailPassword: account.emailPassword, backupEmail: account.backupEmail, backupEmailPassword: account.backupEmailPassword },
              'backup',
              ['microsoft', 'outlook', 'verify', 'security code', 'verification'],
              30000
            );

            if (!code) {
              code = await resolveVerificationCode(account, 'email', ['microsoft', 'outlook', 'verify', 'security code'], 30000);
            }
          }

          if (code) {
            console.log(`  Got verification code: ${code}`);
            const codeInput = needsCode || (await findVisibleLocator(page, CODE_INPUT_SELECTORS, 2000));
            if (codeInput) {
              try {
                await codeInput.fill('');
                for (let i = 0; i < code.length; i++) {
                  await codeInput.type(code[i], { delay: 50 });
                }
                await sleep(300);
                if (!(await clickFirstVisible(page, CONTINUE_BUTTON_SELECTORS, 2500))) {
                  await codeInput.press('Enter');
                }
                console.log(`  Entered verification code`);
              } catch (err) {
                console.log(`  Code entry error: ${err.message}`);
              }
              await sleep(2500);
            }
          } else {
            console.log('  Could not retrieve code via IMAP. Enter it manually in the browser window...');
            codeCheckSource = null;
          }
        }
      }

      let hasCaptcha;
      try {
        hasCaptcha = await page.locator(CAPTCHA_SELECTORS.join(', ')).first();
        await hasCaptcha.waitFor({ state: 'visible', timeout: 1000 });
      } catch {
        hasCaptcha = null;
      }
      if (hasCaptcha && !options.headless) {
        console.log('  CAPTCHA detected — solve manually...');
      }

      await sleep(1000);
    }

    const oauthStatus = await getOAuthStatus().catch(() => ({ status: 'unknown' }));
    let callbackReceived = oauthStatus.status === 'success';
    if (!callbackReceived) {
      try {
        const currentUrl = page.url();
        callbackReceived = currentUrl.includes('localhost:1455/auth/callback') || currentUrl.includes('tightrope://');
      } catch {
        callbackReceived = true;
      }
    }
    if (callbackReceived || oauthStatus.status === 'success') {
      console.log('  Checking if account was added...');
      const newAccount = await waitForAccountAdded(existingEmailSet, 30000);
      if (newAccount) {
        console.log(`  ADDED: ${newAccount.email} (${newAccount.plan_type || 'unknown plan'})`);
        if (knownEmails instanceof Set) {
          knownEmails.add(normalizeEmail(newAccount.email));
        }
        return { email: newAccount.email, status: 'added', plan: newAccount.plan_type };
      }
      console.log('  Account may have been added but could not be confirmed, checking list...');
      const currentAccounts = await listAccounts();
      const currentEmails = new Set(currentAccounts.map((a) => normalizeEmail(a?.email)).filter(Boolean));
      if (currentEmails.has(account.email)) {
        console.log(`  ADDED (confirmed): ${account.email}`);
        if (knownEmails instanceof Set) {
          knownEmails.add(account.email);
        }
        return { email: account.email, status: 'added' };
      }
    }

    console.log('  TIMEOUT: Could not verify account addition');
    try {
      await page.screenshot({ path: `debug-${account.email.replace(/[^a-z0-9]/gi, '_')}.png` });
    } catch {}
    return { email: account.email, status: 'timeout' };
  } catch (err) {
    console.log(`  ERROR: ${err.message}`);
    return { email: account.email, status: 'error', error: err.message };
  } finally {
    await context.close().catch(() => {});
  }
}

async function main() {
  const args = process.argv.slice(2);

  if (args.length === 0 || args.includes('--help') || args.includes('-h')) {
    console.log(`
Usage: node bulk-add-accounts.cjs <accounts-file.txt> [options]

File format (one account per line):
  email:password:emailPassword:backupEmail:backupEmailPassword

  email               — OpenAI account email
  password            — OpenAI account password
  emailPassword       — Password for the email inbox (IMAP access)
  backupEmail         — Recovery/verification email address
  backupEmailPassword — Password for the recovery email (IMAP access)

If emailPassword is omitted, password is used for IMAP login.
Lines starting with # are ignored.

The script automates this flow:
  1. Start Tightrope OAuth → open OpenAI auth URL in browser
  2. Enter email and password
  3. When a verification code is needed:
     a. Connect to the email inbox via IMAP
     b. Look for OpenAI verification code
     c. If the email (Outlook) itself requires verification:
        - Check backup email via IMAP for Outlook/Microsoft verification code
        - Enter that code in Outlook, then get the OpenAI code
  4. Enter the code in OpenAI
  5. Tightrope picks up the account via callback

IMAP servers are auto-detected from email domains:
  outlook.com / hotmail.com / live.com → outlook.office365.com:993
  firstmail.ltd / legenmail.com         → firstmail.ltd:993
  gmail.com                            → imap.gmail.com:993

Options:
  --headless    Run browser in headless mode (default: visible)
  --timeout N   Total timeout per account in seconds (default: 240)

Examples:
  node bulk-add-accounts.cjs accounts.txt
  node bulk-add-accounts.cjs accounts.txt --timeout 300
`);
    process.exit(0);
  }

  const filePath = resolve(args[0]);
  const headless = args.includes('--headless');
  const timeoutIdx = args.indexOf('--timeout');
  const manualTimeout = timeoutIdx >= 0 ? parseInt(args[timeoutIdx + 1], 10) * 1000 : 240000;

  let content;
  try {
    content = await readFile(filePath, 'utf-8');
  } catch (err) {
    console.error(`Failed to read file: ${filePath}`);
    console.error(err.message);
    process.exit(1);
  }

  const seenEmails = new Set();
  const accounts = [];
  let skippedDuplicates = 0;
  for (const line of content.split('\n')) {
    const parsed = parseAccountLine(line);
    if (!parsed) {
      continue;
    }
    if (seenEmails.has(parsed.email)) {
      skippedDuplicates += 1;
      continue;
    }
    seenEmails.add(parsed.email);
    accounts.push(parsed);
  }

  if (accounts.length === 0) {
    console.error('No accounts found in file');
    process.exit(1);
  }

  console.log(`Found ${accounts.length} account(s)`);
  if (skippedDuplicates > 0) {
    console.log(`Skipped ${skippedDuplicates} duplicate account(s) in input file`);
  }
  console.log(`Mode: ${headless ? 'headless' : 'visible'}`);
  console.log(`Timeout: ${manualTimeout / 1000}s per account`);

  try {
    const health = await fetch(`${TIGHTROPE_API}/api/runtime/proxy`);
    if (!health.ok) throw new Error('not ok');
    console.log('Tightrope runtime: OK');
  } catch {
    console.error(`ERROR: Tightrope is not running at ${TIGHTROPE_API}`);
    console.error('Start Tightrope first, then run this script.');
    process.exit(1);
  }

  const browser = await chromium.launch({
    headless,
    args: ['--disable-blink-features=AutomationControlled'],
  });

  const results = [];
  const knownEmails = new Set((await getExistingAccountsByEmail()).keys());

  try {
    for (const account of accounts) {
      const currentAccounts = await getExistingAccountsByEmail();
      for (const email of currentAccounts.keys()) {
        knownEmails.add(email);
      }
      if (knownEmails.has(account.email)) {
        console.log(`\n=== Adding: ${account.email} ===`);
        console.log(`  SKIP: ${account.email} already exists`);
        results.push({ email: account.email, status: 'skipped' });
        continue;
      }

      const result = await addAccount(account, browser, { headless, manualTimeout, knownEmails });
      results.push(result);

      if (result.status === 'added' || result.status === 'skipped') {
        await sleep(2000);
      }
    }
  } finally {
    await browser.close();
  }

  console.log('\n\n=== Results ===');
  const added = results.filter((r) => r.status === 'added').length;
  const skipped = results.filter((r) => r.status === 'skipped').length;
  const failed = results.filter((r) => r.status !== 'added' && r.status !== 'skipped').length;

  for (const r of results) {
    const icon = r.status === 'added' ? '+' : r.status === 'skipped' ? '=' : 'x';
    console.log(`  [${icon}] ${r.email} - ${r.status}${r.plan ? ` (${r.plan})` : ''}${r.error ? `: ${r.error}` : ''}`);
  }

  console.log(`\nAdded: ${added} | Skipped: ${skipped} | Failed: ${failed} | Total: ${results.length}`);

  if (failed > 0) process.exitCode = 1;
}

main().catch((err) => {
  console.error('Fatal error:', err);
  process.exit(1);
});
