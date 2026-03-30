# Electron Desktop Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the placeholder Electron shell with the migrated React renderer while keeping the main/preload/native boundary small and testable.

**Architecture:** Keep Electron main process responsible for lifecycle, IPC registration, native loading, and window management only. Migrate the renderer from `/Users/fabian/Development/codex-lb/frontend` into `app/src/renderer`, preserving feature-level `api.ts` boundaries and progressively adapting them to the preload/native boundary.

**Tech Stack:** Electron, TypeScript, Vitest, React, React Testing Library, the existing frontend code from `/Users/fabian/Development/codex-lb/frontend`.

---

## Chunk 1: App-Side Test Harness And Main/Preload Boundary

### Task 1: Add App-Side Test Infrastructure

**Files:**
- Modify: `app/package.json`
- Create: `app/vitest.config.ts`
- Create: `app/src/main/native.test.ts`
- Create: `app/src/preload/index.test.ts`

- [ ] **Step 1: Write the failing preload/native tests**

```ts
import { describe, expect, it } from 'vitest'

describe('native wrapper', () => {
  it('exposes getHealth', async () => {
    expect(typeof (await import('./native')).native.getHealth).toBe('function')
  })
})
```

- [ ] **Step 2: Run the tests to verify they fail**

Run:
```bash
cd app && npm test
```
Expected: FAIL because app-side test infrastructure does not exist yet.

- [ ] **Step 3: Add Vitest and wire the test script**

Add a stable app-side test command before porting the renderer.

- [ ] **Step 4: Re-run the tests**

Run:
```bash
cd app && npm test
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add app/package.json app/vitest.config.ts app/src/main/native.test.ts app/src/preload/index.test.ts
git commit -m "test: add electron app test harness"
```

### Task 2: Lock Down Main/Preload IPC Contracts

**Files:**
- Modify: `app/src/main/index.ts`
- Modify: `app/src/main/ipc.ts`
- Modify: `app/src/main/native.ts`
- Modify: `app/src/preload/index.ts`
- Create: `app/src/main/ipc.test.ts`

- [ ] **Step 1: Write the failing IPC contract test**

```ts
it('registers health and sync IPC handlers', async () => {
  const module = await import('./ipc')
  expect(typeof module.registerIpcHandlers).toBe('function')
})
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cd app && npm test -- ipc.test.ts
```
Expected: FAIL because the IPC contract is not fully testable yet.

- [ ] **Step 3: Make the main/preload/native boundary explicit and typed**

Expose only:

- health
- cluster status
- sync trigger
- future admin/proxy calls as intentionally versioned IPC surface

- [ ] **Step 4: Re-run the IPC contract test**

Run:
```bash
cd app && npm test -- ipc.test.ts
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add app/src/main/index.ts app/src/main/ipc.ts app/src/main/native.ts app/src/preload/index.ts app/src/main/ipc.test.ts
git commit -m "feat: stabilize electron ipc boundary"
```

## Chunk 2: Renderer Migration And Desktop Packaging

### Task 3: Import The React Renderer And Keep Feature Boundaries

**Files:**
- Create: `app/src/renderer/`
- Create: `app/src/renderer/main.tsx`
- Create: `app/src/renderer/App.tsx`
- Create: `app/src/renderer/test/`
- Modify: `app/package.json`

- [ ] **Step 1: Write the failing renderer smoke test**

```ts
import { render, screen } from '@testing-library/react'
import { App } from './App'

it('renders the dashboard shell', () => {
  render(<App />)
  expect(screen.getByText(/dashboard/i)).toBeInTheDocument()
})
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cd app && npm test -- App.test.tsx
```
Expected: FAIL because the renderer does not exist yet.

- [ ] **Step 3: Copy and adapt the renderer from the reference frontend**

Source:

- `/Users/fabian/Development/codex-lb/frontend/src`
- `/Users/fabian/Development/codex-lb/frontend/package.json`

Keep feature directories and API client boundaries intact.

- [ ] **Step 4: Re-run the renderer smoke test**

Run:
```bash
cd app && npm test -- App.test.tsx
cd app && npm run build
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add app/src/renderer app/package.json
git commit -m "feat: migrate renderer into electron app"
```

### Task 4: Swap Renderer API Clients Onto The Desktop Boundary

**Files:**
- Modify: `app/src/main/ipc.ts`
- Modify: `app/src/preload/index.ts`
- Modify: `app/src/renderer/**/*`
- Create: `app/src/renderer/test/ipc_integration.test.tsx`

- [ ] **Step 1: Write the failing desktop integration test**

```ts
it('loads dashboard data through the preload boundary', async () => {
  render(<App />)
  expect(await screen.findByText(/overview/i)).toBeInTheDocument()
})
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cd app && npm test -- ipc_integration.test.tsx
```
Expected: FAIL.

- [ ] **Step 3: Adapt feature `api.ts` modules to the desktop boundary**

Strategy:

- prefer preload/IPC for local-native-backed operations
- keep plain HTTP only for external proxy clients where required
- do not leak Electron APIs into feature components

- [ ] **Step 4: Re-run the integration test**

Run:
```bash
cd app && npm test -- ipc_integration.test.tsx
cd app && npm run build
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add app/src/main/ipc.ts app/src/preload/index.ts app/src/renderer
git commit -m "feat: connect renderer to native desktop boundary"
```

### Task 5: Add Desktop Packaging Verification

**Files:**
- Modify: `app/package.json`
- Create: `app/electron-builder.yml`
- Create: `app/src/main/runtime_paths.test.ts`

- [ ] **Step 1: Write the failing runtime-path test**

```ts
it('resolves an app-data root for the current platform', async () => {
  const { resolveRuntimePaths } = await import('./runtime_paths')
  expect(resolveRuntimePaths().dataDir.length).toBeGreaterThan(0)
})
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cd app && npm test -- runtime_paths.test.ts
```
Expected: FAIL.

- [ ] **Step 3: Add runtime-path helpers and packaging config**

Cover:

- macOS `~/Library/Application Support/tightrope`
- Windows `%APPDATA%/tightrope`
- Linux `~/.local/share/tightrope`

- [ ] **Step 4: Re-run tests and packaging smoke build**

Run:
```bash
cd app && npm test -- runtime_paths.test.ts
cd app && npm run build
```
Expected: PASS.

- [ ] **Step 5: Commit the slice if the workspace is under git**

```bash
git add app/package.json app/electron-builder.yml app/src/main/runtime_paths.test.ts
git commit -m "build: add desktop packaging verification"
```
