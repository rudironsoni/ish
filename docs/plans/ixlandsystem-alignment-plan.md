# IXLandSystem Alignment Plan for IXLand Linux Runtime

## Objective

Reshape this iSH fork into a forward-only AArch64 Linux runtime aligned with IXLandSystem architecture and constraints:

- TCTI-only guest execution (no alternate guest CPU path).
- Linux-surface-first contracts.
- iOS/Darwin details isolated under `internal/ios/**`.
- Linux headers generated from upstream Linux via Makefile-driven vendoring, not hand-authored shims.

This plan intentionally removes legacy iSH filesystem abstractions (`fakefs`, `realfs`, `fake-db`) rather than preserving backward compatibility.

## Ground Truth from IXLandSystem

The plan is grounded to `https://github.com/rudironsoni/IXLandSystem`:

- `Makefile` target `vendor-linux-headers` performs Linux vendoring directly in Makefile logic.
- Vendored output tuple shape is:
  - `third_party/linux/<version>/<arch>/uapi/include`
  - `third_party/linux/<version>/<arch>/srctree`
  - `third_party/linux/<version>/<arch>/objtree`
- Metadata and integrity outputs:
  - `source.json`
  - `README.md`
  - `manifest.sha256`
- `project.yml` uses tuple-root variables:
  - `LINUX_VENDOR_ROOT`, `LINUX_ROOT`, `LINUX_UAPI_INCLUDE_ROOT`, `LINUX_SRCTREE_ROOT`, `LINUX_OBJTREE_ROOT`
- Host-specific implementations are isolated under `internal/ios/**`.

## End-State Architecture in This Repo

### Runtime structure

- Linux surface remains under `Sources/IXLandLinuxRuntime/**` but is organized by Linux subsystem concerns.
- iOS-specific host bridges move under `Sources/IXLandLinuxRuntime/internal/ios/**`.
- Filesystem semantics are Linux-shaped through a synthetic VFS surface, backed by iOS adapters.

### Build and headers

- Replace current `third_party/linux-uapi/...` usage with IXLandSystem tuple model under `third_party/linux/...`.
- Use Makefile-driven vendoring as the source of truth for generated Linux headers.
- Keep global include paths minimal; expose `srctree/objtree` only where required.

### Runtime constraints

- Preserve guest execution through TCTI only.
- Keep one active guest emulator/session at a time.
- Avoid host-side behavior that acts as an alternate Linux CPU execution engine.

## Migration Phases

### Phase 1: Linux header vendoring parity

1. Port IXLandSystem `vendor-linux-headers` Makefile flow into this repo.
2. Generate tuple output under `third_party/linux/<version>/<arch>/...`.
3. Add tuple validation and metadata outputs (`source.json`, `manifest.sha256`, `README.md`).
4. Update `project.yml` variables and include roots to tuple model.

Acceptance:

- Vendoring target succeeds deterministically.
- UAPI compile smoke checks pass against `LINUX_UAPI_INCLUDE_ROOT`.

### Phase 2: Boundary enforcement (`internal/ios`)

1. Create/normalize `Sources/IXLandLinuxRuntime/internal/ios/**`.
2. Move Darwin/iOS-specific logic out of Linux-surface modules into host bridge modules.
3. Restrict Linux-surface code to Linux/UAPI contracts and runtime abstractions.

Acceptance:

- Linux-surface modules avoid direct Apple-specific API dependencies.
- Header/search path setup cleanly separates Linux surface and host internals.

### Phase 3: Filesystem core replacement

1. Introduce synthetic Linux VFS contract surface (mount, path, inode, fd, attrs, poll/pty hooks).
2. Implement iOS-backed adapters under `internal/ios/fs/**`.
3. Switch bootstrap and mount flow to new VFS root path.

Acceptance:

- Root bootstrap no longer depends on `fakefs`/`realfs`.
- Linux-visible behavior remains standards-aligned for path/open/stat/read/write/dir traversal.

### Phase 4: Legacy abstraction removal

1. Remove `fakefs`, `realfs`, `fake-db` runtime code and related dependencies.
2. Remove fakefs tooling coupling from app and tools.
3. Remove/replace tests that assume fakefs-root conventions.

Acceptance:

- No runtime or app path references to removed abstractions remain.
- Build/test pass with new VFS path.

### Phase 5: Contract and E2E hardening

1. Add Linux contract tests (compile-smoke + behavior contracts) aligned to IXLandSystem style.
2. Keep app-path end-to-end checks for guest session behavior and regressions.
3. Add lint/guard rules that prevent reintroduction of Darwin leakage or legacy FS layers.

Acceptance:

- Contract tests pass for Linux surface.
- App-path guest tests pass without OOM/open bootstrap regressions.

## Files and Areas Impacted

Primary:

- `Makefile` (vendoring target addition/update).
- `project.yml` (tuple variables, include path strategy).
- `Sources/IXLandLinuxRuntime/**` (subsystem refactor, `internal/ios` isolation, VFS replacement).
- `Sources/IXLandTerminal/**` (root bootstrap and rootfs flow updates).
- `Tests/**` (replace fakefs assumptions, add contract coverage).

Legacy code targeted for deletion:

- `Sources/IXLandLinuxRuntime/fs/fake*`
- `Sources/IXLandLinuxRuntime/fs/real*`
- `Sources/IXLandLinuxRuntime/fs/fake-db*`
- fakefs-dependent tool/app plumbing.

## Guardrails

- No backward compatibility requirement for legacy fakefs/realfs/fake-db behavior.
- No hand-authored Linux-looking header trees in vendored output.
- No fallback interpreter or alternate guest instruction execution path outside TCTI.
- Keep Linux semantics authoritative and host behavior encapsulated.

## Suggested PR Stack

1. **Vendoring + project variable migration**
2. **`internal/ios` boundary introduction and host code moves**
3. **New synthetic VFS scaffolding and bootstrap switch**
4. **Legacy FS layer removal + app/tool updates**
5. **Test migration + contract/E2E hardening + lint gates**

## Definition of Done

- Linux header vendoring matches IXLandSystem tuple model and workflow.
- Runtime Linux surface is clearly separated from iOS internals.
- Legacy fakefs/realfs/fake-db stack is removed.
- App boot/session path runs through new synthetic Linux surface with TCTI-only guest execution.
- Contract and E2E tests validate Linux behavior and prevent regression.
