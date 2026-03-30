# Tightrope Rebuild Docs

Derived from `c++.md`, these docs split the monolithic rebuild blueprint into executable tracks.

Execution order:

1. `foundation-runtime`
2. `contract-baseline`
3. `admin-control-plane`
4. `proxy-compatibility`
5. `electron-desktop`
6. `cluster-sync` (post-parity, behind a feature flag)

Documents:

- Spec: `docs/superpowers/specs/2026-03-28-tightrope-rebuild-program-design.md`
- Plan: `docs/superpowers/plans/2026-03-28-contract-baseline.md`
- Plan: `docs/superpowers/plans/2026-03-28-foundation-runtime.md`
- Plan: `docs/superpowers/plans/2026-03-28-admin-control-plane.md`
- Plan: `docs/superpowers/plans/2026-03-28-proxy-compatibility.md`
- Plan: `docs/superpowers/plans/2026-03-28-electron-desktop.md`
- Plan: `docs/superpowers/plans/2026-03-28-cluster-sync.md`

Rules:

- Use `c++.md` as the source blueprint.
- Use `/Users/fabian/Development/codex-lb` as read-only reference.
- Follow `@superpowers:test-driven-development` for all production behavior changes.
- Treat build/package/config wiring as command-driven red/green work: reproduce failure first, then fix.
- Do not start cluster sync before single-node parity is green.
