#!/usr/bin/env node
'use strict';

const path = require('node:path');
const { spawn } = require('node:child_process');

const appDir = path.resolve(__dirname, '..');

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

child.on('exit', (code) => {
  process.exit(typeof code === 'number' ? code : 0);
});
