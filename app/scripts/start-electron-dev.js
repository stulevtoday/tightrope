#!/usr/bin/env node
'use strict';

const path = require('node:path');
const { spawn, spawnSync } = require('node:child_process');

const appDir = path.resolve(__dirname, '..');

// On Windows, Electron loads tightrope-core.node via LoadLibrary which locks the
// file against writes AND against re-loading from a different process instance.
// If a previous dev session left an orphaned electron.exe holding the module,
// the new run either can't rebuild the native module or silently falls back to
// stubs (which produces ECONNREFUSED on every IPC call because no server ever
// starts). Detect and kill such orphans before launching.
function killStaleTightropeElectronProcesses() {
  if (process.platform !== 'win32') return;

  // `tasklist /M <module>` lists all processes that have the named DLL/node file
  // loaded. Output is an "Image Name, PID, Modules" table; in CSV form it's
  // `"electron.exe","12345","..."`. We only need the PIDs.
  const result = spawnSync('tasklist', ['/M', 'tightrope-core.node', '/FO', 'CSV', '/NH'], {
    encoding: 'utf8',
    windowsHide: true,
  });
  if (result.error || typeof result.stdout !== 'string') return;

  const pids = [];
  for (const line of result.stdout.split(/\r?\n/)) {
    const trimmed = line.trim();
    if (!trimmed || trimmed.toLowerCase().startsWith('info:')) continue;
    // CSV columns: "ImageName","PID","Modules"
    const match = trimmed.match(/^"([^"]+)","(\d+)"/);
    if (!match) continue;
    const pid = Number(match[2]);
    if (Number.isFinite(pid) && pid > 0 && pid !== process.pid) {
      pids.push(pid);
    }
  }
  if (pids.length === 0) return;

  console.warn(
    `[dev:electron] killing ${pids.length} stale tightrope electron process(es) holding tightrope-core.node: ${pids.join(', ')}`
  );
  for (const pid of pids) {
    spawnSync('taskkill', ['/F', '/T', '/PID', String(pid)], {
      stdio: 'ignore',
      windowsHide: true,
    });
  }
}

killStaleTightropeElectronProcesses();

let electronBinary;
try {
  electronBinary = require('electron');
} catch (error) {
  const message = error instanceof Error ? error.message : String(error);
  console.error(`[dev:electron] Failed to resolve Electron binary: ${message}`);
  process.exit(1);
}

const childEnv = {
  ...process.env,
  VITE_DEV_SERVER_URL: 'http://127.0.0.1:5173',
  TIGHTROPE_NATIVE_BUILD_MODE: 'debug',
};
if (process.env.TIGHTROPE_DISABLE_NATIVE !== '1') {
  delete childEnv.TIGHTROPE_DISABLE_NATIVE;
}

const child = spawn(electronBinary, ['.'], {
  cwd: appDir,
  stdio: 'inherit',
  env: childEnv,
});

child.on('error', (error) => {
  console.error(`[dev:electron] Failed to start Electron: ${error.message}`);
  process.exit(1);
});

child.on('exit', (code, signal) => {
  if (signal) {
    console.error(`[dev:electron] Electron exited with signal ${signal}`);
    process.exit(1);
  }
  process.exit(typeof code === 'number' ? code : 0);
});
