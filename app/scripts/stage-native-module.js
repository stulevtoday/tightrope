#!/usr/bin/env node
'use strict';

const fs = require('node:fs');
const path = require('node:path');

const appDir = path.resolve(__dirname, '..');
const repoRoot = path.resolve(appDir, '..');
const outputDir = path.join(appDir, 'native');
const outputPath = path.join(outputDir, 'tightrope-core.node');
const nativeModuleFilename = 'tightrope-core.node';

function uniquePaths(paths) {
  return Array.from(new Set(paths.map((candidate) => path.resolve(candidate))));
}

function walkFiles(rootDir, filter, out = []) {
  if (!fs.existsSync(rootDir)) {
    return out;
  }

  const entries = fs.readdirSync(rootDir, { withFileTypes: true });
  for (const entry of entries) {
    const fullPath = path.join(rootDir, entry.name);
    if (entry.isDirectory()) {
      if (entry.name === '.git' || entry.name === 'node_modules' || entry.name === 'dist') {
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

function pathHasSegment(candidate, segment) {
  const normalizedSegment = segment.toLowerCase();
  return path
    .normalize(candidate)
    .split(path.sep)
    .some((part) => part.toLowerCase() === normalizedSegment);
}

function discoveredNativeModuleCandidates() {
  return uniquePaths([path.join(repoRoot, 'build'), path.join(appDir, 'build')])
    .flatMap((root) =>
      walkFiles(root, (fullPath) => {
        return (
          path.basename(fullPath).toLowerCase() === nativeModuleFilename &&
          !pathHasSegment(path.relative(root, fullPath), 'Debug')
        );
      })
    )
    .sort();
}

function nativeModuleCandidates() {
  return uniquePaths([
    path.join(repoRoot, 'build', 'Release', nativeModuleFilename),
    path.join(repoRoot, 'build', nativeModuleFilename),
    path.join(appDir, 'build', 'Release', nativeModuleFilename),
    path.join(appDir, 'build', nativeModuleFilename),
    ...discoveredNativeModuleCandidates(),
  ]);
}

function resolveNativeModulePath() {
  for (const candidate of nativeModuleCandidates()) {
    if (fs.existsSync(candidate)) {
      return candidate;
    }
  }
  return null;
}

function stageNativeModule() {
  const sourcePath = resolveNativeModulePath();
  if (!sourcePath) {
    throw new Error(
      'Could not find a release native module. Run `npm --prefix app run ensure:native:release` first.'
    );
  }

  fs.mkdirSync(outputDir, { recursive: true });
  fs.copyFileSync(sourcePath, outputPath);
  console.log(`[native] staged ${path.relative(appDir, sourcePath)} -> ${path.relative(appDir, outputPath)}`);
}

function main() {
  try {
    stageNativeModule();
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    console.error(`[native] ${message}`);
    process.exit(1);
  }
}

main();
