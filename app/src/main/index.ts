// Electron main process entry point
// Loads tightrope-core.node native module and exposes IPC handlers to renderer

import * as path from 'path';
import { app, BrowserWindow } from 'electron';
import { native } from './native';
import { registerIpcHandlers, submitOauthManualCallback } from './ipc';

let mainWindow: BrowserWindow | null = null;
let nativeShutdownCompleted = false;
let nativeShutdownInFlight: Promise<void> | null = null;
const skipNativeShutdownOnQuit = process.env.TIGHTROPE_SKIP_NATIVE_SHUTDOWN === '1';
const DEEP_LINK_PROTOCOL = 'tightrope';
const pendingDeepLinks: string[] = [];
let deepLinkDrainInFlight: Promise<void> | null = null;

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

async function processDeepLink(raw: string): Promise<void> {
  const parsed = parseDeepLink(raw);
  if (!parsed) {
    return;
  }
  focusMainWindow();
  if (!isOauthCallbackDeepLink(parsed)) {
    return;
  }
  try {
    const response = await submitOauthManualCallback(raw);
    console.info('[tightrope] deep link oauth callback handled', {
      status: response.status,
      callbackUrl: raw,
    });
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

function registerTightropeProtocolClient(): void {
  if (process.defaultApp) {
    if (process.argv.length >= 2) {
      app.setAsDefaultProtocolClient(DEEP_LINK_PROTOCOL, process.execPath, [path.resolve(process.argv[1])]);
    }
    return;
  }
  app.setAsDefaultProtocolClient(DEEP_LINK_PROTOCOL);
}

const hasSingleInstanceLock = app.requestSingleInstanceLock();
if (!hasSingleInstanceLock) {
  app.quit();
}

app.on('second-instance', (_event, argv) => {
  const deepLink = findDeepLinkArg(argv);
  if (deepLink) {
    enqueueDeepLink(deepLink);
  } else {
    focusMainWindow();
  }
});

app.on('open-url', (event, url) => {
  event.preventDefault();
  enqueueDeepLink(url);
});

app.whenReady().then(async () => {
  registerTightropeProtocolClient();

  await native.init({
    host: '127.0.0.1',
    port: 2455,
    oauth_callback_host: 'localhost',
    oauth_callback_port: 1455,
  });
  registerIpcHandlers();

  mainWindow = new BrowserWindow({
    width: 1540,
    height: 940,
    frame: false,
    webPreferences: {
      preload: path.join(__dirname, '../preload/index.js'),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });

  const devServerUrl = process.env.VITE_DEV_SERVER_URL;
  if (devServerUrl) {
    await mainWindow.loadURL(devServerUrl);
  } else {
    const rendererPath = path.join(__dirname, '../renderer/index.html');
    await mainWindow.loadFile(rendererPath);
  }

  const startupDeepLink = findDeepLinkArg(process.argv);
  if (startupDeepLink) {
    enqueueDeepLink(startupDeepLink);
  }
  await flushDeepLinks();
});

app.on('before-quit', (event) => {
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
  if (process.platform !== 'darwin') app.quit();
});
