import fs from 'node:fs';
import path from 'node:path';
import { app, BrowserWindow, dialog, ipcMain, safeStorage } from 'electron';

export type DatabasePassphraseMode = 'setup' | 'unlock';

export interface DatabasePassphraseSelection {
  dbPath: string;
  mode: DatabasePassphraseMode;
  passphrase: string;
}

export class DatabasePassphraseCancelledError extends Error {
  constructor(mode: DatabasePassphraseMode) {
    super(`Database ${mode} cancelled by user`);
    this.name = 'DatabasePassphraseCancelledError';
  }
}

export function isDatabasePassphraseCancelledError(error: unknown): boolean {
  if (error instanceof DatabasePassphraseCancelledError) {
    return true;
  }
  if (!(error instanceof Error)) {
    return false;
  }
  return error.message.toLowerCase().includes('database') && error.message.toLowerCase().includes('cancelled by user');
}

type DatabaseState = 'missing' | 'plaintext' | 'encrypted';

interface PassphrasePromptOptions {
  title: string;
  subtitle: string;
  actionLabel: string;
  errorMessage?: string;
  confirmationRequired: boolean;
}

interface PassphrasePromptResult {
  passphrase: string;
  confirmation: string | null;
}

const SQLITE_PLAINTEXT_HEADER = Buffer.from('SQLite format 3\0', 'utf8');
const DB_PROMPT_DEBUG = process.env.TIGHTROPE_DB_PROMPT_DEBUG === '1' || Boolean(process.env.VITE_DEV_SERVER_URL);
const PASSPHRASE_KEY_FILE = '.passphrase-key';

function debugPrompt(message: string): void {
  if (!DB_PROMPT_DEBUG) {
    return;
  }
  console.info(`[tightrope][db-passphrase] ${message}`);
}

function passphraseKeyPath(dbPath: string): string {
  return dbPath + PASSPHRASE_KEY_FILE;
}

function savePassphrase(dbPath: string, passphrase: string): void {
  try {
    if (!safeStorage.isEncryptionAvailable()) {
      debugPrompt('safeStorage encryption unavailable, skipping passphrase save');
      return;
    }
    const encrypted = safeStorage.encryptString(passphrase);
    const keyPath = passphraseKeyPath(dbPath);
    fs.writeFileSync(keyPath, encrypted);
    debugPrompt(`passphrase saved to ${keyPath}`);
  } catch (err) {
    debugPrompt(`failed to save passphrase: ${err instanceof Error ? err.message : String(err)}`);
  }
}

function loadPassphrase(dbPath: string): string | null {
  try {
    if (!safeStorage.isEncryptionAvailable()) {
      debugPrompt('safeStorage encryption unavailable, cannot load passphrase');
      return null;
    }
    const keyPath = passphraseKeyPath(dbPath);
    if (!fs.existsSync(keyPath)) {
      debugPrompt(`no saved passphrase at ${keyPath}`);
      return null;
    }
    const encrypted = fs.readFileSync(keyPath);
    const passphrase = safeStorage.decryptString(encrypted);
    debugPrompt('passphrase loaded from saved key');
    return passphrase;
  } catch (err) {
    debugPrompt(`failed to load passphrase: ${err instanceof Error ? err.message : String(err)}`);
    return null;
  }
}

export function deleteSavedPassphrase(dbPath: string): void {
  try {
    const keyPath = passphraseKeyPath(dbPath);
    if (fs.existsSync(keyPath)) {
      fs.unlinkSync(keyPath);
      debugPrompt(`deleted saved passphrase at ${keyPath}`);
    }
  } catch (err) {
    debugPrompt(`failed to delete saved passphrase: ${err instanceof Error ? err.message : String(err)}`);
  }
}

function escapeHtml(value: string): string {
  return value
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;');
}

export function resolveDatabasePath(): string {
  const configured = process.env.TIGHTROPE_DB_PATH?.trim();
  if (configured && configured.length > 0) {
    return path.resolve(configured);
  }
  return path.join(app.getPath('userData'), 'store.db');
}

function detectDatabaseState(dbPath: string): DatabaseState {
  try {
    if (!fs.existsSync(dbPath)) {
      return 'missing';
    }
    const stats = fs.statSync(dbPath);
    if (!stats.isFile() || stats.size < SQLITE_PLAINTEXT_HEADER.length) {
      return 'missing';
    }

    const descriptor = fs.openSync(dbPath, 'r');
    try {
      const header = Buffer.alloc(SQLITE_PLAINTEXT_HEADER.length);
      const bytesRead = fs.readSync(descriptor, header, 0, header.length, 0);
      if (bytesRead < SQLITE_PLAINTEXT_HEADER.length) {
        return 'missing';
      }
      return header.equals(SQLITE_PLAINTEXT_HEADER) ? 'plaintext' : 'encrypted';
    } finally {
      fs.closeSync(descriptor);
    }
  } catch {
    return 'missing';
  }
}

function buildPromptHtml(channel: string, options: PassphrasePromptOptions): string {
  const escapedTitle = escapeHtml(options.title);
  const escapedSubtitle = escapeHtml(options.subtitle);
  const escapedError = options.errorMessage ? escapeHtml(options.errorMessage) : '';
  const escapedActionLabel = escapeHtml(options.actionLabel);

  const confirmationField = options.confirmationRequired
    ? `<label>Confirm passphrase<input id="confirmation" type="password" autocomplete="new-password" /></label>`
    : '';

  const errorBlock = escapedError.length > 0
    ? `<div id="error" class="error">${escapedError}</div>`
    : '<div id="error" class="error error--hidden"></div>';

  return `<!doctype html>
<html>
  <head>
    <meta charset="UTF-8" />
    <title>${escapedTitle}</title>
    <style>
      :root {
        color-scheme: dark;
      }
      body {
        margin: 0;
        font-family: "SF Pro Display", "Avenir Next", "Segoe UI", sans-serif;
        background: radial-gradient(circle at top left, #1b2721 0%, #0d1117 50%, #090d12 100%);
        color: #d6e1d5;
        overflow: hidden;
      }
      .shell {
        box-sizing: border-box;
        width: 100vw;
        height: 100vh;
        display: flex;
        flex-direction: column;
        border: 1px solid rgba(117, 145, 120, 0.32);
        border-radius: 12px;
        overflow: hidden;
        background: linear-gradient(180deg, rgba(9, 13, 11, 0.92), rgba(10, 15, 13, 0.9));
      }
      .prompt-titlebar {
        height: 34px;
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 0 10px;
        background: rgba(13, 20, 16, 0.8);
        border-bottom: 1px solid rgba(123, 148, 128, 0.2);
        -webkit-app-region: drag;
      }
      .prompt-titlecopy {
        display: inline-flex;
        align-items: baseline;
        gap: 7px;
        font-size: 11px;
        color: rgba(201, 218, 204, 0.78);
        letter-spacing: 0.2px;
      }
      .prompt-titlecopy strong {
        font-size: 11.5px;
        font-weight: 650;
        color: rgba(225, 238, 228, 0.92);
      }
      .prompt-titlecopy span {
        font-size: 10.5px;
        color: rgba(163, 183, 168, 0.82);
      }
      .titlebar-controls {
        display: inline-flex;
        align-items: center;
        gap: 6px;
        -webkit-app-region: no-drag;
      }
      .titlebar-control {
        width: 11px;
        height: 11px;
        border-radius: 999px;
        border: 0;
        padding: 0;
        cursor: pointer;
        box-shadow: inset 0 0 0 1px rgba(18, 26, 21, 0.44), 0 0 0 1px rgba(0, 0, 0, 0.18);
      }
      .titlebar-control.close {
        background: #f15f56;
      }
      .titlebar-control.minimize {
        background: #e3bf3a;
      }
      .content {
        box-sizing: border-box;
        padding: 16px 20px 20px;
        display: flex;
        flex-direction: column;
        justify-content: center;
        gap: 12px;
        flex: 1;
      }
      h1 {
        margin: 0;
        font-size: 18px;
        font-weight: 640;
      }
      p {
        margin: 0;
        color: #9fb0a0;
        font-size: 13px;
        line-height: 1.45;
      }
      form {
        display: flex;
        flex-direction: column;
        gap: 10px;
      }
      label {
        display: flex;
        flex-direction: column;
        gap: 6px;
        font-size: 12px;
        color: #b7c9b8;
      }
      input {
        border: 1px solid rgba(141, 176, 147, 0.42);
        background: rgba(15, 22, 18, 0.82);
        color: #e2efe2;
        border-radius: 8px;
        padding: 9px 10px;
        font-size: 13px;
        outline: none;
      }
      input:focus {
        border-color: rgba(156, 210, 167, 0.8);
        box-shadow: 0 0 0 1px rgba(156, 210, 167, 0.35);
      }
      .buttons {
        display: flex;
        justify-content: flex-end;
        gap: 8px;
        margin-top: 6px;
      }
      button {
        border: 1px solid rgba(126, 150, 129, 0.38);
        background: rgba(17, 24, 19, 0.9);
        color: #ceddce;
        border-radius: 7px;
        font-size: 12px;
        font-weight: 610;
        padding: 7px 12px;
        cursor: pointer;
      }
      button.primary {
        background: linear-gradient(180deg, rgba(84, 142, 99, 0.95), rgba(63, 112, 77, 0.95));
        border-color: rgba(111, 171, 124, 0.95);
        color: #f2fbf1;
      }
      .error {
        font-size: 12px;
        color: #ff9f9f;
        min-height: 16px;
      }
      .error--hidden {
        visibility: hidden;
      }
    </style>
  </head>
  <body>
    <div class="shell">
      <div class="prompt-titlebar">
        <div class="titlebar-controls" aria-label="Window controls">
          <button class="titlebar-control close" id="window-close" type="button" aria-label="Close window"></button>
          <button class="titlebar-control minimize" id="window-minimize" type="button" aria-label="Minimize window"></button>
        </div>
        <div class="prompt-titlecopy"><strong>tightrope</strong><span>database security</span></div>
      </div>
      <div class="content">
        <h1>${escapedTitle}</h1>
        <p>${escapedSubtitle}</p>
        ${errorBlock}
        <form id="passphrase-form">
          <label>Database passphrase<input id="passphrase" type="password" autocomplete="off" ${options.confirmationRequired ? 'minlength="8"' : ''} /></label>
          ${confirmationField}
          <div class="buttons">
            <button type="button" id="cancel">Cancel</button>
            <button type="submit" class="primary">${escapedActionLabel}</button>
          </div>
        </form>
      </div>
    </div>
    <script>
      const { ipcRenderer } = require('electron');
      const channel = ${JSON.stringify(channel)};
      const form = document.getElementById('passphrase-form');
      const passphraseInput = document.getElementById('passphrase');
      const confirmationInput = document.getElementById('confirmation');
      const cancelButton = document.getElementById('cancel');
      const minimizeButton = document.getElementById('window-minimize');
      const closeButton = document.getElementById('window-close');
      const errorNode = document.getElementById('error');

      const setError = (message) => {
        if (!errorNode) return;
        if (!message) {
          errorNode.textContent = '';
          errorNode.classList.add('error--hidden');
          return;
        }
        errorNode.textContent = message;
        errorNode.classList.remove('error--hidden');
      };

      const submit = () => {
        const passphrase = passphraseInput ? passphraseInput.value : '';
        const confirmation = confirmationInput ? confirmationInput.value : null;

        if (!passphrase) {
          setError('Passphrase is required.');
          return;
        }

        if (confirmationInput) {
          if (passphrase.length < 8) {
            setError('Passphrase must be at least 8 characters.');
            return;
          }
          if (confirmation === null || passphrase !== confirmation) {
            setError('Passphrases do not match.');
            return;
          }
        }

        setError('');
        ipcRenderer.send(channel, {
          passphrase,
          confirmation,
        });
      };

      if (cancelButton) {
        cancelButton.addEventListener('click', () => {
          ipcRenderer.send(channel, { cancel: true });
        });
      }
      if (minimizeButton) {
        minimizeButton.addEventListener('click', () => {
          ipcRenderer.send(channel, { action: 'minimize' });
        });
      }
      if (closeButton) {
        closeButton.addEventListener('click', () => {
          ipcRenderer.send(channel, { action: 'close' });
        });
      }

      if (form) {
        form.addEventListener('submit', (event) => {
          event.preventDefault();
          submit();
        });
      }

      window.addEventListener('keydown', (event) => {
        if (event.key === 'Escape') {
          ipcRenderer.send(channel, { cancel: true });
        }
      });

      if (passphraseInput) {
        passphraseInput.focus();
      }
    </script>
  </body>
</html>`;
}

async function showPassphrasePrompt(options: PassphrasePromptOptions): Promise<PassphrasePromptResult | null> {
  return new Promise<PassphrasePromptResult | null>((resolve) => {
    const channel = `database-passphrase:${Date.now()}:${Math.random().toString(16).slice(2)}`;
    debugPrompt(`open mode=${options.confirmationRequired ? 'setup' : 'unlock'} channel=${channel}`);
    const window = new BrowserWindow({
      width: 500,
      height: options.confirmationRequired ? 380 : 338,
      frame: false,
      resizable: false,
      minimizable: true,
      maximizable: false,
      fullscreenable: false,
      show: false,
      backgroundColor: '#0a1110',
      autoHideMenuBar: true,
      webPreferences: {
        nodeIntegration: true,
        contextIsolation: false,
        sandbox: false,
      },
    });

    let resolved = false;
    let onChannelMessage: ((event: unknown, payload: unknown) => void) | null = null;
    const finish = (result: PassphrasePromptResult | null): void => {
      if (resolved) {
        return;
      }
      resolved = true;
      if (onChannelMessage) {
        ipcMain.removeListener(channel, onChannelMessage);
        onChannelMessage = null;
      }
      if (!window.isDestroyed()) {
        window.close();
      }
      resolve(result);
    };

    onChannelMessage = (_event, payload: unknown) => {
      if (!payload || typeof payload !== 'object') {
        debugPrompt(`payload_invalid channel=${channel}`);
        finish(null);
        return;
      }
      const message = payload as { action?: unknown; cancel?: unknown; passphrase?: unknown; confirmation?: unknown };
      if (message.action === 'minimize') {
        if (!window.isDestroyed()) {
          window.minimize();
        }
        return;
      }
      if (message.action === 'close') {
        debugPrompt(`window_close_action channel=${channel}`);
        finish(null);
        return;
      }
      if (message.cancel === true) {
        debugPrompt(`cancelled channel=${channel}`);
        finish(null);
        return;
      }
      const passphraseLength = typeof message.passphrase === 'string' ? message.passphrase.length : -1;
      const hasConfirmation = typeof message.confirmation === 'string';
      debugPrompt(`submitted channel=${channel} passphrase_len=${passphraseLength} has_confirmation=${hasConfirmation}`);
      finish({
        passphrase: typeof message.passphrase === 'string' ? message.passphrase : '',
        confirmation: typeof message.confirmation === 'string' ? message.confirmation : null,
      });
    };
    ipcMain.on(channel, onChannelMessage);

    window.once('closed', () => {
      debugPrompt(`window_closed channel=${channel}`);
      finish(null);
    });

    window.once('ready-to-show', () => {
      debugPrompt(`ready channel=${channel}`);
      window.show();
    });

    window.webContents.on('did-fail-load', (_event, errorCode, errorDescription) => {
      debugPrompt(`load_failed channel=${channel} code=${errorCode} error=${errorDescription}`);
    });

    window.webContents.on('console-message', (_event, level, message) => {
      debugPrompt(`renderer_console channel=${channel} level=${level} msg=${message}`);
    });

    const html = buildPromptHtml(channel, options);
    void window.loadURL(`data:text/html;charset=UTF-8,${encodeURIComponent(html)}`).catch(() => {
      debugPrompt(`load_exception channel=${channel}`);
      finish(null);
    });
  });
}

async function confirmSetupNotice(migratingExisting: boolean): Promise<void> {
  const detail = migratingExisting
    ? 'Existing local data will be encrypted with this password. If you forget it, recovery is not possible.'
    : 'This password protects local data. If you forget it, recovery is not possible.';

  const response = await dialog.showMessageBox({
    type: 'warning',
    title: 'Secure Database Password Required',
    message: 'Create and remember your database password.',
    detail,
    buttons: ['Continue', 'Quit'],
    defaultId: 0,
    cancelId: 1,
    noLink: true,
  });

  if (response.response !== 0) {
    throw new DatabasePassphraseCancelledError('setup');
  }
}

async function promptForSetupPassphrase(migratingExisting: boolean): Promise<string> {
  await confirmSetupNotice(migratingExisting);

  let errorMessage: string | undefined;
  while (true) {
    const response = await showPassphrasePrompt({
      title: 'Set Database Password',
      subtitle: 'This password unlocks Tightrope and protects local data.',
      actionLabel: 'Set Password',
      errorMessage,
      confirmationRequired: true,
    });

    if (response === null) {
      throw new DatabasePassphraseCancelledError('setup');
    }

    if (response.passphrase.length < 8) {
      errorMessage = 'Passphrase must be at least 8 characters.';
      continue;
    }

    if (response.confirmation === null || response.passphrase !== response.confirmation) {
      errorMessage = 'Passphrases do not match.';
      continue;
    }

    return response.passphrase;
  }
}

export async function promptForUnlockPassphrase(errorMessage?: string): Promise<string> {
  let currentError = errorMessage;
  while (true) {
    const response = await showPassphrasePrompt({
      title: 'Unlock Database',
      subtitle: 'Enter your database password.',
      actionLabel: 'Unlock',
      errorMessage: currentError,
      confirmationRequired: false,
    });

    if (response === null) {
      throw new DatabasePassphraseCancelledError('unlock');
    }

    if (response.passphrase.length === 0) {
      currentError = 'Passphrase is required.';
      continue;
    }

    return response.passphrase;
  }
}

export async function requestDatabasePassphrase(): Promise<DatabasePassphraseSelection> {
  const dbPath = resolveDatabasePath();
  fs.mkdirSync(path.dirname(dbPath), { recursive: true });
  const state = detectDatabaseState(dbPath);

  if (state === 'encrypted') {
    const savedPassphrase = loadPassphrase(dbPath);
    if (savedPassphrase !== null) {
      debugPrompt('attempting unlock with saved passphrase');
      return {
        dbPath,
        mode: 'unlock',
        passphrase: savedPassphrase,
      };
    }
    const passphrase = await promptForUnlockPassphrase();
    return {
      dbPath,
      mode: 'unlock',
      passphrase,
    };
  }

  if (state === 'missing') {
    const confirmed = await dialog.showMessageBox({
      type: 'question',
      title: 'New Database',
      message: 'No existing database found.',
      detail:
        'A new encrypted database will be created at:\n\n' +
        dbPath +
        '\n\n' +
        'If you already have a database, make sure it is at this path before continuing. ' +
        'Creating a new database will start fresh with no accounts.',
      buttons: ['Create New Database', 'Quit'],
      defaultId: 0,
      cancelId: 1,
      noLink: true,
    });
    if (confirmed.response !== 0) {
      throw new DatabasePassphraseCancelledError('setup');
    }
  }

  const passphrase = await promptForSetupPassphrase(state === 'plaintext');
  return {
    dbPath,
    mode: 'setup',
    passphrase,
  };
}

export { savePassphrase as savePassphraseToKeychain };
