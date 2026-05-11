// Electron main process entry point
// Loads tightrope-core.node native module and exposes IPC handlers to renderer

import * as path from 'path';
import * as fs from 'fs';
import { app, BrowserWindow, Menu, type MenuItemConstructorOptions } from 'electron';
import { ensureTightropeCodexSetup } from './codexSetup';
import {
  isDatabasePassphraseCancelledError,
  promptForUnlockPassphrase,
  requestDatabasePassphrase,
  savePassphraseToKeychain,
  deleteSavedPassphrase,
} from './databasePassphrase';
import { native } from './native';
import { registerIpcHandlers, submitOauthManualCallback } from './ipc';

let mainWindow: BrowserWindow | null = null;
let nativeShutdownCompleted = false;
let nativeShutdownInFlight: Promise<void> | null = null;
let nativeInitialized = false;
let startupCompleted = false;
const skipNativeShutdownOnQuit = process.env.TIGHTROPE_SKIP_NATIVE_SHUTDOWN === '1';
const DEEP_LINK_PROTOCOL = 'tightrope';
const pendingDeepLinks: string[] = [];
let deepLinkDrainInFlight: Promise<void> | null = null;
const isDevSession = Boolean(process.env.VITE_DEV_SERVER_URL);
const DEV_RENDERER_RETRYABLE_ERRORS = new Set([
  'ERR_CONNECTION_REFUSED',
  'ERR_CONNECTION_RESET',
  'ERR_CONNECTION_TIMED_OUT',
  'ERR_NAME_NOT_RESOLVED',
]);

function shouldAutoOpenDevTools(): boolean {
  const explicit = process.env.TIGHTROPE_OPEN_DEVTOOLS;
  if (explicit === '1') {
    return true;
  }
  if (explicit === '0') {
    return false;
  }

  if (isDevSession) {
    return true;
  }

  const nodeEnv = (process.env.NODE_ENV ?? '').toLowerCase();
  if (nodeEnv === 'development' || nodeEnv === 'debug') {
    return true;
  }

  const buildType = (process.env.TIGHTROPE_BUILD_TYPE ?? process.env.BUILD_TYPE ?? '').toLowerCase();
  return buildType === 'debug';
}

if (isDevSession) {
  const devUserDataPath = path.resolve(process.cwd(), '.electron-dev');
  fs.mkdirSync(devUserDataPath, { recursive: true });
  app.setPath('userData', devUserDataPath);
}

function isTightropeDeepLink(value: string): boolean {
  return value.toLowerCase().startsWith(`${DEEP_LINK_PROTOCOL}://`);
}

function parseDeepLink(value: string): URL | null {
  try {
    return new URL(value);
  } catch {
    return null;
  }
}

function isOauthCallbackDeepLink(url: URL): boolean {
  const host = url.hostname.toLowerCase();
  const pathname = url.pathname.toLowerCase();
  return (
    ((host === 'oauth' || host === 'auth') && pathname === '/callback') ||
    pathname === '/oauth/callback' ||
    pathname === '/auth/callback'
  );
}

function isOauthSuccessDeepLink(url: URL): boolean {
  const host = url.hostname.toLowerCase();
  const pathname = url.pathname.toLowerCase();
  return (
    ((host === 'oauth' || host === 'auth') && pathname === '/success') ||
    pathname === '/oauth/success' ||
    pathname === '/auth/success'
  );
}

function emitOauthDeepLink(kind: 'success' | 'callback', url: string): void {
  if (!mainWindow || mainWindow.isDestroyed()) {
    return;
  }
  mainWindow.webContents.send('oauth:deep-link', { kind, url });
}

function findDeepLinkArg(argv: readonly string[]): string | null {
  return argv.find((value) => isTightropeDeepLink(value)) ?? null;
}

function focusMainWindow(): void {
  if (!mainWindow) {
    return;
  }
  if (mainWindow.isMinimized()) {
    mainWindow.restore();
  }
  if (!mainWindow.isVisible()) {
    mainWindow.show();
  }
  mainWindow.focus();
}

function emitAboutMenuRequested(): void {
  if (!mainWindow || mainWindow.isDestroyed()) {
    return;
  }
  focusMainWindow();
  mainWindow.webContents.send('app:about-open');
}

function installApplicationMenu(): void {
  if (process.platform !== 'darwin') {
    return;
  }

  const viewSubmenu: MenuItemConstructorOptions[] = app.isPackaged
    ? []
    : [{ role: 'reload' }, { role: 'forceReload' }, { role: 'toggleDevTools' }];

  const template: MenuItemConstructorOptions[] = [
    {
      label: app.name,
      submenu: [
        { label: `About ${app.name}`, click: () => emitAboutMenuRequested() },
        { type: 'separator' },
        { role: 'services' },
        { type: 'separator' },
        { role: 'hide' },
        { role: 'hideOthers' },
        { role: 'unhide' },
        { type: 'separator' },
        { role: 'quit' },
      ],
    },
    {
      label: 'Edit',
      submenu: [
        { role: 'undo' },
        { role: 'redo' },
        { type: 'separator' },
        { role: 'cut' },
        { role: 'copy' },
        { role: 'paste' },
        { role: 'selectAll' },
      ],
    },
    ...(viewSubmenu.length > 0 ? [{ label: 'View', submenu: viewSubmenu } as MenuItemConstructorOptions] : []),
    {
      label: 'Window',
      submenu: [{ role: 'minimize' }, { role: 'zoom' }, { type: 'separator' }, { role: 'front' }],
    },
    {
      label: 'Help',
      submenu: [{ label: `About ${app.name}`, click: () => emitAboutMenuRequested() }],
    },
  ];

  Menu.setApplicationMenu(Menu.buildFromTemplate(template));
}

async function processDeepLink(raw: string): Promise<void> {
  const parsed = parseDeepLink(raw);
  if (!parsed) {
    return;
  }
  focusMainWindow();
  if (isOauthSuccessDeepLink(parsed)) {
    emitOauthDeepLink('success', raw);
    return;
  }
  if (!isOauthCallbackDeepLink(parsed)) {
    return;
  }
  try {
    const response = await submitOauthManualCallback(raw);
    console.info('[tightrope] deep link oauth callback handled', {
      status: response.status,
      callbackUrl: raw,
    });
    emitOauthDeepLink('callback', raw);
  } catch (error) {
    console.error('[tightrope] deep link oauth callback failed', { callbackUrl: raw, error });
  }
}

async function flushDeepLinks(): Promise<void> {
  if (deepLinkDrainInFlight || !mainWindow || pendingDeepLinks.length === 0) {
    return;
  }

  deepLinkDrainInFlight = (async () => {
    while (pendingDeepLinks.length > 0) {
      const next = pendingDeepLinks.shift();
      if (!next) {
        break;
      }
      await processDeepLink(next);
    }
  })().finally(() => {
    deepLinkDrainInFlight = null;
    if (pendingDeepLinks.length > 0 && mainWindow) {
      void flushDeepLinks();
    }
  });

  await deepLinkDrainInFlight;
}

function enqueueDeepLink(url: string): void {
  if (!isTightropeDeepLink(url)) {
    return;
  }
  pendingDeepLinks.push(url);
  void flushDeepLinks();
}

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => {
    setTimeout(resolve, ms);
  });
}

function loadErrorCode(error: unknown): string | null {
  if (!error || typeof error !== 'object') {
    return null;
  }
  const maybeCode = (error as { code?: unknown }).code;
  return typeof maybeCode === 'string' ? maybeCode : null;
}

async function loadRenderer(mainWindow: BrowserWindow, devServerUrl: string | undefined): Promise<void> {
  if (!devServerUrl) {
    const rendererPath = path.join(__dirname, '../renderer/index.html');
    await mainWindow.loadFile(rendererPath);
    return;
  }

  const maxAttempts = 30;
  const retryDelayMs = 500;
  for (let attempt = 1; attempt <= maxAttempts; attempt += 1) {
    try {
      await mainWindow.loadURL(devServerUrl);
      return;
    } catch (error) {
      const code = loadErrorCode(error);
      const retryable = code !== null && DEV_RENDERER_RETRYABLE_ERRORS.has(code);
      if (!retryable || attempt === maxAttempts) {
        throw error;
      }
      console.warn(
        `[tightrope] renderer dev server unavailable (${code}); retrying (${attempt}/${maxAttempts}) in ${retryDelayMs}ms`,
      );
      await delay(retryDelayMs);
    }
  }
}

function registerTightropeProtocolClient(): void {
  if (isDevSession && process.env.TIGHTROPE_REGISTER_PROTOCOL_IN_DEV !== '1') {
    return;
  }

  const isDefaultApp = process.defaultApp;
  if (process.defaultApp) {
    if (process.argv.length < 2) {
      return;
    }
    app.setAsDefaultProtocolClient(DEEP_LINK_PROTOCOL, process.execPath, [path.resolve(process.argv[1])]);
    return;
  }

  if (!isDefaultApp) {
    app.setAsDefaultProtocolClient(DEEP_LINK_PROTOCOL);
  }
}

const enforceSingleInstanceLock = !isDevSession || process.env.TIGHTROPE_ENFORCE_SINGLE_INSTANCE === '1';
if (enforceSingleInstanceLock) {
  const hasSingleInstanceLock = app.requestSingleInstanceLock();
  if (!hasSingleInstanceLock) {
    app.quit();
  } else {
    app.on('second-instance', (_event, argv) => {
      const deepLink = findDeepLinkArg(argv);
      if (deepLink) {
        enqueueDeepLink(deepLink);
      } else {
        focusMainWindow();
      }
    });
  }
}

app.on('open-url', (event, url) => {
  event.preventDefault();
  enqueueDeepLink(url);
});

app.whenReady().then(async () => {
  registerTightropeProtocolClient();
  await ensureTightropeCodexSetup();

  const dbPassphraseSelection = await requestDatabasePassphrase();
  const databasePassphraseMode = dbPassphraseSelection.mode;
  const databasePath = dbPassphraseSelection.dbPath;
  process.env.TIGHTROPE_DB_PATH = databasePath;
  let sessionDbPassphrase = dbPassphraseSelection.passphrase;
  dbPassphraseSelection.passphrase = '';
  const maxUnlockAttempts = 5;
  for (let attempt = 0; ; attempt += 1) {
    try {
      await native.init({
        host: '127.0.0.1',
        port: 2455,
        oauth_callback_host: 'localhost',
        oauth_callback_port: 1455,
        db_path: databasePath,
        db_passphrase: sessionDbPassphrase,
      });
      nativeInitialized = true;
      savePassphraseToKeychain(databasePath, sessionDbPassphrase);
      break;
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      const unlockFailed = message.toLowerCase().includes('database unlock failed');
      if (databasePassphraseMode !== 'unlock' || !unlockFailed || attempt + 1 >= maxUnlockAttempts) {
        if (unlockFailed) {
          deleteSavedPassphrase(databasePath);
        }
        throw error;
      }
      deleteSavedPassphrase(databasePath);
      sessionDbPassphrase = await promptForUnlockPassphrase('Unable to unlock database with that password.');
    }
  }
  sessionDbPassphrase = '';
  registerIpcHandlers();
  const windowIconPath = path.join(app.getAppPath(), 'src', 'renderer', 'assets', 'load_balancer.png');
  const windowIcon = fs.existsSync(windowIconPath) ? windowIconPath : undefined;

  mainWindow = new BrowserWindow({
    width: 1540,
    height: 940,
    frame: false,
    icon: app.isPackaged && process.platform === 'darwin' ? undefined : windowIcon,
    webPreferences: {
      preload: path.join(__dirname, '../preload/index.js'),
      contextIsolation: true,
      nodeIntegration: false,
      devTools: !app.isPackaged,
    },
  });
  installApplicationMenu();

  native.registerSyncEventCallback((event) => {
    if (!mainWindow || mainWindow.isDestroyed()) return;
    mainWindow.webContents.send('sync:event', event);
  });

  const devServerUrl = process.env.VITE_DEV_SERVER_URL;
  try {
    await loadRenderer(mainWindow, devServerUrl);
  } catch (error) {
    console.error('[tightrope] renderer load failed', error);
  }
  if (!mainWindow.isDestroyed() && shouldAutoOpenDevTools()) {
    mainWindow.webContents.openDevTools({ mode: 'detach' });
  }

  const startupDeepLink = findDeepLinkArg(process.argv);
  if (startupDeepLink) {
    enqueueDeepLink(startupDeepLink);
  }
  await flushDeepLinks();
  startupCompleted = true;
}).catch((error) => {
  if (isDatabasePassphraseCancelledError(error)) {
    console.info('[tightrope] startup cancelled by user');
    app.quit();
    return;
  }
  console.error('[tightrope] startup fatal', error);
  process.exitCode = 1;
  app.quit();
});

app.on('before-quit', (event) => {
  try {
    native.unregisterSyncEventCallback();
  } catch (error) {
    console.warn('[tightrope] failed to unregister sync callback during quit', error);
  }

  if (!nativeInitialized) {
    nativeShutdownCompleted = true;
    return;
  }

  if (skipNativeShutdownOnQuit) {
    return;
  }

  if (nativeShutdownCompleted) {
    return;
  }

  if (typeof native.shutdownSync === 'function') {
    try {
      native.shutdownSync();
    } catch (error) {
      console.error('[tightrope] native shutdownSync failed', error);
    }
    nativeShutdownCompleted = true;
    return;
  }

  event.preventDefault();
  if (nativeShutdownInFlight) {
    return;
  }

  nativeShutdownInFlight = native
    .shutdown()
    .catch((error) => {
      console.error('[tightrope] native shutdown failed', error);
    })
    .finally(() => {
      nativeShutdownCompleted = true;
      nativeShutdownInFlight = null;
      app.quit();
    });
});

app.on('window-all-closed', () => {
  if (!startupCompleted) {
    return;
  }
  app.quit();
});
