#!/usr/bin/env node
'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const appDir = path.resolve(__dirname, '..');
const repoRoot = path.resolve(appDir, '..');

const forceRebuild = process.argv.includes('--force');
const dryRun = process.argv.includes('--dry-run');
const modeArg = process.argv.find((arg) => arg.startsWith('--mode='));

function resolveBuildMode() {
  const explicitMode = modeArg ? modeArg.slice('--mode='.length) : null;
  const rawMode = explicitMode || process.env.TIGHTROPE_NATIVE_BUILD_MODE || 'release';
  const normalized = String(rawMode).trim().toLowerCase();
  if (normalized === 'debug' || normalized === 'release') {
    return normalized;
  }
  throw new Error(`Unsupported native build mode "${rawMode}". Use --mode=debug or --mode=release.`);
}

const buildMode = resolveBuildMode();
// On Windows the vcpkg triplet x64-windows-static-md only ships release-CRT libs
// (/MD), and several ports in our graph (nuraft, SQLiteCpp, tomlplusplus, libuv) do
// not produce a debug variant at all. Mixing a Debug-config tightrope-core (_DEBUG,
// _ITERATOR_DEBUG_LEVEL=2) with release-CRT static libs produces LNK2038 runtime-
// library mismatches. Aliasing debug -> release on Windows gives us a single
// buildable native module that still loads fine into Electron's release runtime.
const effectiveBuildMode = process.platform === 'win32' ? 'release' : buildMode;
const cmakeConfig = effectiveBuildMode === 'debug' ? 'Debug' : 'Release';
// IMPORTANT: Keep these mode->output mappings in lockstep with app/src/main/native.ts.
// Debug must stay isolated in build-electron-debug (do NOT reuse build-debug, which is
// used by non-cmake-js/test builds and can produce incompatible addon binaries).
const cmakeOutDir = effectiveBuildMode === 'debug' ? '../build-electron-debug' : '../build';

function nativeModuleCandidatesForMode(mode = effectiveBuildMode) {
  if (mode === 'debug') {
    return [
      path.join(repoRoot, 'build-electron-debug', 'Debug', 'tightrope-core.node'),
      path.join(repoRoot, 'build-electron-debug', 'tightrope-core.node'),
      path.join(appDir, 'build-electron-debug', 'Debug', 'tightrope-core.node'),
      path.join(appDir, 'build-electron-debug', 'tightrope-core.node'),
    ];
  }

  return [
    path.join(repoRoot, 'build', 'Release', 'tightrope-core.node'),
    path.join(repoRoot, 'build', 'tightrope-core.node'),
    path.join(appDir, 'build', 'Release', 'tightrope-core.node'),
    path.join(appDir, 'build', 'tightrope-core.node'),
  ];
}

function metadataPathFor(modulePath) {
  return `${modulePath}.buildinfo.json`;
}

function readBuildMetadata(modulePath) {
  const metadataPath = metadataPathFor(modulePath);
  if (!fs.existsSync(metadataPath)) {
    return null;
  }
  try {
    const raw = fs.readFileSync(metadataPath, 'utf8');
    const parsed = JSON.parse(raw);
    if (!parsed || typeof parsed !== 'object') {
      return null;
    }
    return parsed;
  } catch {
    return null;
  }
}

function writeBuildMetadata(modulePath, metadata) {
  const metadataPath = metadataPathFor(modulePath);
  fs.writeFileSync(metadataPath, `${JSON.stringify(metadata, null, 2)}\n`, 'utf8');
}

function walkFiles(rootDir, filter, out = []) {
  if (!fs.existsSync(rootDir)) {
    return out;
  }

  const entries = fs.readdirSync(rootDir, { withFileTypes: true });
  for (const entry of entries) {
    const fullPath = path.join(rootDir, entry.name);
    if (entry.isDirectory()) {
      if (
        entry.name === '.git' ||
        entry.name === 'node_modules' ||
        entry.name === 'build' ||
        entry.name === 'build-debug' ||
        entry.name === 'dist'
      ) {
        continue;
      }
      walkFiles(fullPath, filter, out);
      continue;
    }

    if (filter(fullPath)) {
      out.push(fullPath);
    }
  }

  return out;
}

function newestMtimeMs(paths) {
  let newest = 0;
  for (const candidate of paths) {
    if (!candidate || !fs.existsSync(candidate)) {
      continue;
    }
    const mtime = fs.statSync(candidate).mtimeMs;
    if (mtime > newest) {
      newest = mtime;
    }
  }
  return newest;
}

function existingModulePaths() {
  return nativeModuleCandidatesForMode().filter((candidate) => fs.existsSync(candidate));
}

function unresolvedProjectSymbols(modulePath) {
  // On macOS we link Node-API modules with -undefined dynamic_lookup, which can
  // accidentally hide missing internal symbols until runtime. Guard against that.
  if (process.platform !== 'darwin') {
    return [];
  }

  const result = spawnSync('nm', ['-m', modulePath], {
    cwd: repoRoot,
    env: buildSpawnEnv(),
    encoding: 'utf8',
  });

  if (result.error || (typeof result.status === 'number' && result.status !== 0)) {
    return [];
  }

  const output = typeof result.stdout === 'string' ? result.stdout : '';
  return output
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => /\(undefined\)\s+external\s+__ZN9tightrope/.test(line));
}

function assertNoUnresolvedProjectSymbols(modulePaths) {
  const offenders = [];
  for (const modulePath of modulePaths) {
    const symbols = unresolvedProjectSymbols(modulePath);
    if (symbols.length > 0) {
      offenders.push({
        modulePath,
        symbols,
      });
    }
  }

  if (offenders.length === 0) {
    return;
  }

  const details = offenders
    .map(({ modulePath, symbols }) => {
      const preview = symbols.slice(0, 8).join('\n  ');
      const suffix = symbols.length > 8 ? `\n  ... and ${symbols.length - 8} more` : '';
      return `${modulePath}\n  ${preview}${suffix}`;
    })
    .join('\n');

  throw new Error(
    `native module contains unresolved tightrope symbols (likely stale CMake source discovery):\n${details}`
  );
}

function resolveTriplet() {
  const archMap = {
    x64: 'x64',
    arm64: 'arm64',
  };

  const arch = archMap[process.arch];
  if (!arch) {
    throw new Error(`Unsupported CPU architecture: ${process.arch}`);
  }

  if (process.platform === 'darwin') {
    return {
      triplet: `${arch}-osx-static`,
      overlayTriplets: '../triplets',
    };
  }

  if (process.platform === 'linux') {
    return {
      triplet: `${arch}-linux-static`,
      overlayTriplets: '../triplets',
    };
  }

  if (process.platform === 'win32') {
    return {
      triplet: `${arch}-windows-static-md`,
      overlayTriplets: null,
    };
  }

  throw new Error(`Unsupported platform: ${process.platform}`);
}

function readElectronVersion() {
  const electronPkgPath = path.join(appDir, 'node_modules', 'electron', 'package.json');
  if (!fs.existsSync(electronPkgPath)) {
    throw new Error('Electron is not installed. Run `npm --prefix app install` first.');
  }

  const pkg = JSON.parse(fs.readFileSync(electronPkgPath, 'utf8'));
  if (!pkg.version) {
    throw new Error('Unable to resolve Electron version from app/node_modules/electron/package.json.');
  }

  return {
    version: pkg.version,
    packagePath: electronPkgPath,
  };
}

function expectedBuildMetadata() {
  const { version } = readElectronVersion();
  return {
    runtime: 'electron',
    runtimeVersion: version,
    platform: process.platform,
    arch: process.arch,
    buildMode: effectiveBuildMode,
  };
}

function metadataMatches(metadata, expected) {
  if (!metadata || typeof metadata !== 'object') {
    return false;
  }

  return (
    metadata.runtime === expected.runtime &&
    metadata.runtimeVersion === expected.runtimeVersion &&
    metadata.platform === expected.platform &&
    metadata.arch === expected.arch &&
    metadata.buildMode === expected.buildMode
  );
}

function buildSpawnEnv() {
  const env = {};
  for (const [key, value] of Object.entries(process.env)) {
    if (!key || key.startsWith('=')) {
      continue;
    }
    if (typeof value === 'undefined') {
      continue;
    }
    env[key] = value;
  }
  return env;
}

function pruneStaleWindowsBuildDir(buildDir) {
  // On Windows, an aborted cmake-js configure can leave behind a CMakeCache.txt
  // without the generated *.vcxproj files. cmake-js then skips configure on the
  // next run and MSBuild fails with "MSB1009: Project file does not exist".
  // Detect that state and wipe the directory so configure runs from scratch.
  if (process.platform !== 'win32') {
    return;
  }
  if (!fs.existsSync(buildDir)) {
    return;
  }
  const cachePath = path.join(buildDir, 'CMakeCache.txt');
  if (!fs.existsSync(cachePath)) {
    return;
  }
  const hasVcxproj = walkFiles(buildDir, (fullPath) => /\.vcxproj$/i.test(fullPath)).length > 0;
  if (hasVcxproj) {
    return;
  }
  console.log(`[native] stale build directory detected (no .vcxproj); removing ${buildDir}`);
  fs.rmSync(buildDir, { recursive: true, force: true });
}

function runBuild() {
  const { version: electronVersion } = readElectronVersion();
  const metadata = expectedBuildMetadata();
  const { triplet, overlayTriplets } = resolveTriplet();
  const cmakeJsCli = path.join(appDir, 'node_modules', 'cmake-js', 'bin', 'cmake-js');

  if (!fs.existsSync(cmakeJsCli)) {
    throw new Error('cmake-js is not installed. Run `npm --prefix app install` first.');
  }

  pruneStaleWindowsBuildDir(path.resolve(appDir, cmakeOutDir));

  const toolchainFileFromEnv = process.env.CMAKE_TOOLCHAIN_FILE;
  const defaultToolchainPath = path.join(repoRoot, 'vcpkg', 'scripts', 'buildsystems', 'vcpkg.cmake');
  const toolchainFile =
    toolchainFileFromEnv ||
    (fs.existsSync(defaultToolchainPath) ? '../vcpkg/scripts/buildsystems/vcpkg.cmake' : null);

  const cmakeArgs = [
    'build',
    '-d',
    '..',
    '--out',
    cmakeOutDir,
    '-r',
    'electron',
    '-v',
    electronVersion,
    '--config',
    cmakeConfig,
    `--CDVCPKG_TARGET_TRIPLET=${process.env.VCPKG_TARGET_TRIPLET || triplet}`,
  ];

  if (overlayTriplets && fs.existsSync(path.join(repoRoot, 'triplets'))) {
    cmakeArgs.push(`--CDVCPKG_OVERLAY_TRIPLETS=${overlayTriplets}`);
  }

  if (toolchainFile) {
    cmakeArgs.push(`--CDCMAKE_TOOLCHAIN_FILE=${toolchainFile}`);
  }

  const installOptions = process.env.VCPKG_INSTALL_OPTIONS || (process.platform === 'darwin' ? '--allow-unsupported' : '');
  if (installOptions) {
    cmakeArgs.push(`--CDVCPKG_INSTALL_OPTIONS=${installOptions}`);
  }

  if (process.platform === 'win32' && !process.env.CMAKE_MSVC_RUNTIME_LIBRARY) {
    cmakeArgs.push('--CDCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL');
  }

  const command = process.execPath;
  const commandArgs = [cmakeJsCli, ...cmakeArgs];
  console.log(
    `[native] Building tightrope-core.node (${effectiveBuildMode} mode; node ${path.relative(appDir, cmakeJsCli)} ${cmakeArgs.join(' ')})`
  );
  const result = spawnSync(command, commandArgs, {
    cwd: appDir,
    stdio: 'inherit',
    env: buildSpawnEnv(),
  });

  if (typeof result.status === 'number' && result.status !== 0) {
    process.exit(result.status);
  }

  if (result.error) {
    throw result.error;
  }

  const builtOutputs = existingModulePaths();
  if (builtOutputs.length === 0) {
    throw new Error('Build succeeded but tightrope-core.node was not found in expected output paths.');
  }

  assertNoUnresolvedProjectSymbols(builtOutputs);

  for (const modulePath of builtOutputs) {
    writeBuildMetadata(modulePath, metadata);
  }

  console.log(`[native] Output: ${builtOutputs[0]}`);
}

function shouldRebuild() {
  const outputs = existingModulePaths();
  if (outputs.length === 0) {
    return { rebuild: true, reason: 'native module is missing' };
  }

  const expectedMetadata = expectedBuildMetadata();
  const hasMatchingMetadata = outputs.some((modulePath) => {
    const metadata = readBuildMetadata(modulePath);
    return metadataMatches(metadata, expectedMetadata);
  });

  if (!hasMatchingMetadata) {
    return {
      rebuild: true,
      reason: `native module metadata is missing or does not match current Electron platform/runtime/mode (${effectiveBuildMode})`,
    };
  }

  const hasUnresolvedSymbols = outputs.some((modulePath) => unresolvedProjectSymbols(modulePath).length > 0);
  if (hasUnresolvedSymbols) {
    return {
      rebuild: true,
      reason: 'native module has unresolved internal symbols',
    };
  }

  const { packagePath: electronPkgPath } = readElectronVersion();
  const sourceFiles = [
    path.join(repoRoot, 'CMakeLists.txt'),
    path.join(repoRoot, 'CMakePresets.json'),
    path.join(repoRoot, 'vcpkg.json'),
    path.join(appDir, 'package.json'),
    path.join(appDir, 'package-lock.json'),
    electronPkgPath,
    ...walkFiles(path.join(repoRoot, 'native'), (fullPath) =>
      /\.(c|cc|cpp|cxx|h|hh|hpp|ipp|inl|cmake|txt)$/i.test(fullPath)
    ),
    ...walkFiles(path.join(repoRoot, 'triplets'), (fullPath) => /\.cmake$/i.test(fullPath)),
  ];

  const newestSourceMtime = newestMtimeMs(sourceFiles);
  const newestOutputMtime = newestMtimeMs(outputs);

  if (newestOutputMtime >= newestSourceMtime) {
    return { rebuild: false, reason: 'native module is up to date' };
  }

  return { rebuild: true, reason: 'native module is older than native source/build inputs' };
}

function main() {
  try {
    if (buildMode !== effectiveBuildMode) {
      console.log(`[native] target mode=${buildMode} aliased to ${effectiveBuildMode} on ${process.platform}`);
    }
    console.log(`[native] target mode=${effectiveBuildMode} config=${cmakeConfig}`);
    if (forceRebuild) {
      if (dryRun) {
        console.log('[native] --dry-run: force rebuild requested.');
        return;
      }
      runBuild();
      return;
    }

    const { rebuild, reason } = shouldRebuild();
    if (!rebuild) {
      console.log(`[native] ${reason}.`);
      return;
    }

    console.log(`[native] ${reason}; rebuilding.`);
    if (dryRun) {
      return;
    }

    runBuild();
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    console.error(`[native] ${message}`);
    process.exit(1);
  }
}

main();
