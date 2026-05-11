#!/usr/bin/env node

const { chromium } = require('playwright');
const { readFile } = require('node:fs/promises');
const { resolve } = require('node:path');
const tls = require('node:tls');
const net = require('node:net');

const TIGHTROPE_API = 'http://127.0.0.1:2455';

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
      const connectOptions = {
        host: this.host,
        port: this.port,
        rejectUnauthorized: false,
      };

      const onConnect = () => {
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
        this.connected = false;
      });

      this.waitUntagged(/^.*/).then((greeting) => {
        this.socket.removeListener('error', reject);
        resolve(greeting);
      }).catch(reject);
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
  return {
    email: parts[0].trim(),
    password: parts[1].trim(),
    emailPassword: parts[2]?.trim() || parts[1].trim(),
    backupEmail: parts[3]?.trim() || '',
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

async function waitForAccountAdded(existingEmails, timeoutMs = 60000) {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    const accounts = await listAccounts();
    const currentEmails = new Set(accounts.map((a) => a.email));
    for (const email of currentEmails) {
      if (!existingEmails.has(email)) {
        return accounts.find((a) => a.email === email);
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
  const { headless } = options;
  console.log(`\n=== Adding: ${account.email} ===`);

  const existingEmails = new Set((await listAccounts()).map((a) => a.email));
  if (existingEmails.has(account.email)) {
    console.log(`  SKIP: ${account.email} already exists`);
    return { email: account.email, status: 'skipped' };
  }

  await stopOAuthFlow();
  await new Promise((r) => setTimeout(r, 500));

  const oauthData = await startOAuthFlow();
  if (!oauthData.authorizationUrl) {
    console.log(`  ERROR: No authorization URL received`);
    return { email: account.email, status: 'error' };
  }

  console.log('  Opening auth URL...');
  const context = await browser.newContext();
  const page = await context.newPage();

  try {
    await page.goto(oauthData.authorizationUrl, { waitUntil: 'domcontentloaded', timeout: 30000 });
    await page.waitForTimeout(1500);

    const emailInput = await page.$('input[name="email"], input[type="email"], input[id="email-input"]');
    if (emailInput) {
      await emailInput.fill(account.email);
      await page.waitForTimeout(300);
      const continueBtn = await page.$('button[type="submit"]') || await page.$('button[data-testid="login-button"]');
      if (continueBtn) await continueBtn.click();
      else await emailInput.press('Enter');
      console.log(`  Email entered: ${account.email}`);
      await page.waitForTimeout(2000);
    }

    const passwordInput = await page.$('input[type="password"], input[name="password"]');
    if (passwordInput) {
      console.log('  Password field found, entering password...');
      await passwordInput.fill(account.password);
      await page.waitForTimeout(300);
      const submitBtn = await page.$('button[type="submit"]');
      if (submitBtn) await submitBtn.click();
      else await passwordInput.press('Enter');
      await page.waitForTimeout(2000);
    }

    const maxAuthTime = 240000;
    const authStart = Date.now();
    let lastCodeAttempt = 0;
    let codeCheckSource = null;

    while (Date.now() - authStart < maxAuthTime) {
      const url = page.url();

      if (url.includes('localhost:1455/auth/callback') || url.includes('tightrope://')) {
        console.log('  Callback received!');
        break;
      }

      const oauthStatus = await getOAuthStatus();
      if (oauthStatus.status === 'success') {
        console.log('  OAuth status: success');
        break;
      }

      const needsCode = await page.$('input[name="code"], input[aria-label*="code" i], input[placeholder*="code" i], input[type="tel"][maxlength="6"]');
      const codeHint = await page.$('text=/enter the code|verification code|verify your email|we sent a code|enter the verification/i');

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
            const codeInput = needsCode || await page.$('input[type="tel"], input[maxlength="6"]');
            if (codeInput) {
              await codeInput.fill('');
              for (let i = 0; i < code.length; i++) {
                await codeInput.type(code[i], { delay: 50 });
              }
              await page.waitForTimeout(300);
              const submitBtn = await page.$('button[type="submit"]');
              if (submitBtn) await submitBtn.click();
              else await codeInput.press('Enter');
              console.log(`  Entered verification code`);
              await page.waitForTimeout(3000);
            }
          } else {
            console.log('  Could not retrieve code via IMAP. Enter it manually in the browser window...');
            codeCheckSource = null;
          }
        }
      }

      const hasCaptcha = await page.$('[data-testid="captcha"], .cf-turnstile, iframe[src*="captcha"], iframe[src*="challenges.cloudflare.com"]');
      if (hasCaptcha && !headless) {
        console.log('  CAPTCHA detected — solve manually...');
      }

      await page.waitForTimeout(1000);
    }

    const oauthStatus = await getOAuthStatus();
    if (oauthStatus.status === 'success' || page.url().includes('localhost:1455/auth/callback') || page.url().includes('tightrope://')) {
      const newAccount = await waitForAccountAdded(existingEmails, 30000);
      if (newAccount) {
        console.log(`  ADDED: ${newAccount.email} (${newAccount.plan_type || 'unknown plan'})`);
        return { email: newAccount.email, status: 'added', plan: newAccount.plan_type };
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
Usage: node bulk-add-accounts.mjs <accounts-file.txt> [options]

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
  node bulk-add-accounts.mjs accounts.txt
  node bulk-add-accounts.mjs accounts.txt --timeout 300
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

  const accounts = content.split('\n').map(parseAccountLine).filter(Boolean);

  if (accounts.length === 0) {
    console.error('No accounts found in file');
    process.exit(1);
  }

  console.log(`Found ${accounts.length} account(s)`);
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

  try {
    for (const account of accounts) {
      const result = await addAccount(account, browser, { headless, manualTimeout });
      results.push(result);

      if (result.status === 'added' || result.status === 'skipped') {
        await new Promise((r) => setTimeout(r, 2000));
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