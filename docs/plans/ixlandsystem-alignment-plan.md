# IXLandKernel / IXLandHostAdapter Alignment Plan for `ish`

## Purpose

This document is the current execution baseline for aligning this `ish` checkout with the refactored upstream `IXLandSystem` architecture on `main`.

Upstream is now explicitly split into:

- `IXLandKernel`
- `IXLandHostAdapter`
- `IXLandKernelTests`
- `IXLandHostAdapterTests`

That split is the target architecture for this repo as well.

The local goal is not folder-name imitation. The local goal is to make a future merge with upstream technically plausible by correcting ownership:

- Linux semantics must converge toward a kernel-owned side.
- Darwin and iOS mechanics must converge toward a private host-adapter side.
- app and UI code must stop acting as an ambient runtime substrate.
- tests must distinguish Linux semantic proof from host seam proof.
- TCTI-only guest execution must remain intact throughout.

## Upstream Reference Snapshot

This plan is grounded against the current upstream repository:

- `https://github.com/rudironsoni/IXLandSystem/tree/main`

Relevant upstream structure:

- `IXLandKernel/`
- `IXLandHostAdapter/`
- `IXLandKernelTests/`
- `IXLandHostAdapterTests/`
- `project.yml`
- `Makefile`
- `third_party/linux/<version>/<arch>/...`

Important upstream kernel-owner patterns:

- `IXLandKernel/fs/**`
- `IXLandKernel/kernel/**`
- `IXLandKernel/runtime/**`
- vendored Linux headers under `uapi/include`, `kheaders/source`, and `kheaders/generated`

Important upstream host-adapter patterns:

- `IXLandHostAdapter/internal/ios/fs/**`
- `IXLandHostAdapter/internal/ios/kernel/**`
- `IXLandHostAdapter/internal/ios/runtime/**`
- narrow subsystem files such as `path_host.c`, `errno_host.c`, and `open_flags.c`

Important upstream proof split:

1. kernel semantics proof
2. host-adapter seam proof
3. header compile-smoke proof
4. product-path proof

All local decisions in this plan must stay compatible with those patterns.

## Current Local Baseline

### Local proto-kernel side

The closest current local analogue to a future `IXLandKernel` side is:

- `Sources/IXLandLinuxRuntime/fs/`
- `Sources/IXLandLinuxRuntime/kernel/`
- `Sources/IXLandLinuxRuntime/emu/`
- `Sources/IXLandLinuxRuntime/tcti/`
- `Sources/IXLandLinuxRuntime/include/`
- `Sources/IXLandLinuxRuntime/util/`

This side still mixes:

- Linux-facing semantics
- syscall and runtime ABI ownership
- guest PTY and session behavior
- Darwin host mediation in places where it should not live
- legacy iSH bootstrap assumptions

### Local proto-host-adapter side

The closest current local analogue to a future `IXLandHostAdapter` side is:

- `internal/ios/fs/`
- `internal/ios/kernel/`
- `internal/ios/platform/`

This side has started to converge toward upstream shape. Current extracted files already include:

- `internal/ios/fs/path_host.c`
- `internal/ios/fs/errno_host.c`
- `internal/ios/fs/open_flags.c`
- `internal/ios/fs/root_bootstrap.c`
- `internal/ios/fs/root_archive.m`
- `internal/ios/fs/root_registry.m`
- `internal/ios/fs/root_store.m`
- `internal/ios/fs/rootfs.c`
- `internal/ios/fs/rootfs_metadata.c`

### App shell side

The app shell remains primarily under:

- `Sources/IXLandTerminal/`

Important runtime-adjacent app files still include:

- `AppDelegate.m`
- `TerminalViewController.m`
- `Terminal.m`
- `LinuxInterop.c`
- `LinuxPTY.c`
- `LinuxTTY.c`
- `Roots.m`
- `iOSFS.m`

This is still too much ambient ownership for a repo that is supposed to converge toward a kernel/host-adapter split.

## Post-`fakefs` Reality

The plan must be grounded in current repo truth, not the older migration story.

Current truth:

- `fakefs`, `fake-db`, and the old fakefs archive layout are no longer the live substrate contract.
- `rootfs` is now the active root backing path.
- Linux metadata is currently persisted through host-private overlay storage in `internal/ios/fs/rootfs_metadata.*`.
- vendored Linux headers already follow the upstream-style tuple layout under `third_party/linux/6.12/arm64`.

Therefore the primary remaining problem is no longer “remove fakefs.”
The primary remaining problem is that Linux semantics, host mechanics, and app orchestration are still mixed across the wrong seams.

## Current Merge Blockers

The main blockers to future mergeability with upstream are now these specific ownership violations:

- `Sources/IXLandLinuxRuntime/fs/real.c`
  still owns too much Darwin-backed path, errno, fd, and filesystem behavior.
- `Sources/IXLandLinuxRuntime/fs/tty-real.c`
  still couples host TTY mechanics directly to Linux-owner runtime paths.
- `internal/ios/fs/rootfs.c`
  still mixes Linux-facing filesystem semantics with host-backed transport details.
- `internal/ios/fs/rootfs_metadata.c`
  is still part of a transitional contract and must remain host-private, not become Linux semantic truth.
- `Sources/IXLandTerminal/LinuxInterop.c`
  still mixes guest session semantics, PTY ownership, stdio setup, host terminal binding, and app-consumed bootstrap shape.
- `Sources/IXLandTerminal/LinuxPTY.c`
  still acts as an app-owned PTY bridge instead of a clearly bounded host-adapter seam.
- `Sources/IXLandTerminal/LinuxTTY.c`
  still participates in runtime-terminal ownership that should be narrowed.
- `Sources/IXLandTerminal/Terminal.m`
  still mixes UI terminal behavior with guest I/O bridge responsibilities.
- `Sources/IXLandTerminal/TerminalViewController.m`
  still owns too much runtime session policy and startup behavior.
- `Sources/IXLandTerminal/AppDelegate.m`
  still owns too much bootstrap and runtime readiness policy.

These are the real remaining blockers. The plan must optimize around them.

## Non-Negotiable Constraints

- Guest AArch64 execution remains TCTI-only.
- Do not introduce or revive an alternate guest CPU engine.
- Linux-facing behavior must not be defined by Darwin semantics.
- Host mechanics must remain explicit and private.
- `project.yml` remains the authoritative build specification.
- `iPhone 17` remains the default simulator proof target.
- `/Volumes/1TB/Xcode/DerivedData` remains the default DerivedData path.
- vendored Linux headers remain the contract source:
  - `third_party/linux/<version>/<arch>/uapi/include`
  - `third_party/linux/<version>/<arch>/kheaders/source`
  - `third_party/linux/<version>/<arch>/kheaders/generated`
- upstream `IXLandSystem` remains the mandatory structural reference.

## Architectural Mapping

### Linux-owner side

Treat these local areas as the proto-`IXLandKernel` side:

- `Sources/IXLandLinuxRuntime/fs/`
- `Sources/IXLandLinuxRuntime/kernel/`
- `Sources/IXLandLinuxRuntime/include/`
- Linux-facing parts of `Sources/IXLandLinuxRuntime/emu/`
- Linux-facing parts of `Sources/IXLandLinuxRuntime/tcti/`

This side must own:

- Linux VFS semantics
- Linux task, session, credential, and signal semantics
- syscall-facing runtime ownership
- guest PTY and job-control semantics
- Linux-defined flags, constants, structures, and ABI contracts

This side must move away from:

- direct Darwin and iOS host APIs
- app-owned bootstrap rules
- host-driven policy decisions
- ad hoc local Linux stand-ins when vendored headers already define the contract

### Host-adapter side

Treat these local areas as the proto-`IXLandHostAdapter` side:

- `internal/ios/fs/`
- `internal/ios/kernel/`
- `internal/ios/platform/`

This side must own host mechanics only:

- host path mediation
- host errno translation
- host-backed storage mechanics
- host-private metadata persistence
- host signal, clock, and sync bridges
- narrow PTY and terminal-object mediation for the app runtime

This side must not become the owner of Linux semantics.

### App shell side

Treat `Sources/IXLandTerminal/` as app shell.

It may own:

- UI
- lifecycle
- terminal presentation
- root catalog and import/export user flows
- app startup and restart orchestration

It must stop owning:

- Linux semantic policy
- ambient filesystem/runtime contracts
- guest session semantic ownership
- host path translation or host metadata rules

## Vendored Header Rule for Refactoring

The vendored Linux tuple is now part of the refactoring contract, not just a build artifact.

Fresh agents must follow these rules:

1. Prefer vendored Linux-defined constants, structs, macros, and include forms over local redefinitions.
2. If a contract is Linux-defined and already present in the vendored tuple, default ownership is kernel-owner unless a narrow host-mechanics reason exists.
3. Do not introduce new local compatibility headers when the tuple already provides the contract surface.
4. Preserve upstream-style include direction as much as possible to keep later file-level merges low-noise.
5. Kernel-header-facing refactors must resolve against `LINUX_KHEADERS_SOURCE_ROOT` and `LINUX_KHEADERS_GENERATED_ROOT`, not invented alternate roots.

## Hosted Guest Session Boundary

The hosted guest-session path is now the highest-risk ownership seam in the repo.

The plan must treat it as a first-class subsystem, not as incidental glue.

Current relevant local files:

- `Sources/IXLandTerminal/LinuxInterop.c`
- `Sources/IXLandTerminal/LinuxPTY.c`
- `Sources/IXLandTerminal/LinuxTTY.c`
- `Sources/IXLandTerminal/Terminal.m`
- `Sources/IXLandTerminal/TerminalViewController.m`
- `Sources/IXLandTerminal/AppDelegate.m`

### Intended ownership split

Kernel-owner side must own:

- Linux session semantics
- PTY semantics
- controlling-terminal semantics
- stdio semantics
- guest-visible input and output behavior
- guest process startup and task/session transitions

Host-adapter side must own:

- PTY allocation bridges to iOS-hosted terminal objects
- narrow host callbacks for byte transport, resize, and hangup
- host-only wiring between guest TTY endpoints and app terminal instances
- private host readiness, synchronization, and bridge mechanics

App shell must own:

- session start requests
- view lifecycle
- restart and presentation UX
- wiring the active UI to a runtime session

App shell must not own runtime semantic decisions.

### Immediate local consequence

`linux_start_session` is currently a mixed seam.

It must be decomposed into:

1. kernel-visible session/bootstrap behavior
2. host-adapter PTY and terminal bridge behavior
3. app-shell orchestration only

The remaining hosted runtime failures should be debugged and fixed only through that split, not through more ad hoc repro variants.

## `rootfs` End-State Contract

`rootfs` is now the current transitional root backing contract.
It is not a license to build a new permanent local filesystem taxonomy.

Required interpretation:

- `rootfs` is the current live substrate for guest root backing.
- `rootfs.c` must stop accumulating Linux semantic decisions that belong in kernel-owner code.
- host-private persistence details such as xattr-backed Linux metadata storage belong in host-adapter-private code.
- Linux-facing inode, stat, mknod, setattr, and related behavior must move toward kernel-owner logic even if the underlying bytes remain host-backed for now.
- no new branded abstraction stack should be introduced unless upstream has an equivalent pattern.

This means the immediate objective is not to replace `rootfs` again.
The immediate objective is to shrink its host-private role until future merge work is low-noise.

## Local Build Target Split

Current state:

- one mixed local runtime target still compiles both kernel-owner and host-adapter-owner code
- `internal/ios/**` is already included, but there is not yet a true local kernel/host-adapter target split

Required future state:

- a kernel-owner build product analogous to `IXLandKernel`
- a host-adapter build product analogous to `IXLandHostAdapter`
- kernel-facing tests separated from host-adapter-private tests

Required migration order:

1. keep source ownership clear before mass target churn
2. move private host seams under `internal/ios/**`
3. carve dedicated target membership in `project.yml`
4. stop compiling app compatibility translation units as implementation owners
5. regenerate the Xcode project only after `project.yml` truth is updated

Compatibility files that should eventually disappear or become header-only shims include:

- `Sources/IXLandTerminal/iOSFS.m`
- any remaining app-layer translation units that exist only to preserve the old mixed layout

## Canonical Test Layering

The local proof model must now be decision-complete.

### Layer 1: Linux semantic runtime tests

These prove direct Linux-visible behavior, including:

- rootfs open, readdir, stat, and fd behavior
- session and PTY semantic behavior where the Linux side is the owner
- syscall-adjacent and VFS-adjacent runtime semantics

### Layer 2: Host-adapter seam tests

These prove host-private mechanics, including:

- path mediation
- errno translation
- open-flag translation
- host metadata persistence
- backing storage mechanics

These do not substitute for Linux semantic proof.

### Layer 3: Hosted session startup tests

These prove the app-style session contract:

- runtime bootstrap
- `become_new_init_child`
- `linux_start_session`
- prompt appearance
- non-interactive guest output
- interactive input routing

### Layer 4: Terminal E2E tests

These prove actual guest-visible shell execution on the app path.

### Canonical repro policy

The bloated hosted repro portfolio must be collapsed to canonical cases only:

- one direct rootfs semantic proof
- one app-style prompt proof
- one app-style non-interactive guest output proof
- one app-style interactive command proof

Additional variants are allowed only when they isolate a distinct contract.
They are not allowed merely as another UI input style experiment.

## Anti-Goals

This plan explicitly rejects:

- renaming folders to `IXLandKernel` / `IXLandHostAdapter` before ownership is corrected
- moving code into `internal/ios/**` without narrowing responsibility
- keeping Linux semantics in app files while claiming the host adapter already exists
- building another local substrate vocabulary unrelated to upstream shape
- citing host tests as proof of Linux semantics
- weakening TCTI constraints to make restructuring easier
- keeping stale repro experiments that no longer reflect the real app path

## Execution Tranches

The tranches below replace the older fakefs-era ordering.

### Tranche 0: Keep the plan and inventory true

Objective:
Keep this document synchronized with upstream structure and current local repo truth.

Required work:

1. Keep the blocker list current.
2. Keep the local proto-kernel, proto-host-adapter, and app-shell inventory current.
3. Prevent future work from assuming the split already exists locally.

Success conditions:

- the plan matches current repo truth
- a fresh agent can use this document directly without rediscovering the architecture

### Tranche 1: Finish hosted guest-session boundary decomposition

Objective:
Make session startup, PTY wiring, and terminal I/O ownership explicit and correctly placed.

Required work:

1. Decompose `linux_start_session` into kernel-owner behavior, host-adapter bridge behavior, and app-shell orchestration.
2. Narrow `LinuxPTY.c`, `LinuxTTY.c`, and `Terminal.m` so they stop owning mixed semantic and host-bridge behavior.
3. Ensure app-style session tests use the real bootstrap contract instead of stale harness shapes.
4. Keep guest-visible PTY semantics Linux-owned.

Success conditions:

- hosted startup ownership is explicit
- prompt, output, and input failures can be localized to kernel, host-adapter, or app-shell layers immediately

### Tranche 2: Extract remaining Darwin host mechanics from `real.c` and `tty-real.c`

Objective:
Remove large mixed Darwin/Linux seams from Linux-owner paths.

Required work:

1. Continue extracting path, errno, flag, and host-I/O mechanics from `real.c` into host-adapter-private files.
2. Move host TTY and raw-host-console mechanics out of `tty-real.c` ownership.
3. Keep Linux-visible semantics in Linux-owner files while narrowing host bridges.
4. Resolve refactors against the vendored Linux tuple rather than local stand-ins.

Success conditions:

- `real.c` and `tty-real.c` are no longer broad mixed seams
- host-specific logic has a clear home under `internal/ios/**`

### Tranche 3: Split `rootfs` Linux semantics from host persistence mechanics

Objective:
Reduce `rootfs` to a narrow host-backed transport and persistence role.

Required work:

1. Keep host-private metadata storage in host-adapter-private code.
2. Move Linux semantic decisions out of `rootfs.c` where they belong on the kernel side.
3. Prevent `rootfs_metadata.*` from becoming the semantic source of truth for Linux-visible behavior.
4. Keep archive and root-store flows aligned with the same contract.

Success conditions:

- `rootfs` remains the live substrate
- host-private persistence is narrow
- Linux semantic ownership no longer depends on host-private code

### Tranche 4: Shrink app-shell runtime ownership

Objective:
Make `AppDelegate`, `TerminalViewController`, `Terminal`, and `Roots` look like app shell, not substrate owners.

Required work:

1. Remove runtime-semantic ownership from app bootstrap and session policy.
2. Keep root catalog, root import/export user flows, and UI presentation app-owned only where they are truly app concerns.
3. Push reusable runtime or host mechanics behind narrower host-adapter seams.

Success conditions:

- app files wire the system together
- app files do not define runtime contracts

### Tranche 5: Express the local kernel / host-adapter target split in `project.yml`

Objective:
Translate the ownership split into real build-target boundaries.

Required work:

1. define local kernel-owner target membership
2. define local host-adapter target membership
3. separate kernel-facing tests from host-adapter-private tests
4. remove compatibility translation units as implementation owners where possible
5. regenerate the Xcode project from `project.yml`

Success conditions:

- `project.yml` expresses a meaningful split
- source and test ownership are visible at build-target level

### Tranche 6: Collapse the test portfolio into canonical kernel, host-adapter, hosted-session, and E2E layers

Objective:
Make the proof model match the architecture instead of preserving historical repro noise.

Required work:

1. keep one canonical direct runtime rootfs proof
2. keep one canonical app-style prompt proof
3. keep one canonical app-style non-interactive guest-output proof
4. keep one canonical app-style interactive command proof
5. keep host seam tests focused on host-private mechanics
6. keep E2E tests focused on guest-visible behavior

Success conditions:

- each test has a clear layer and ownership purpose
- stale session variants no longer dominate the test surface

## Fresh-Agent Operating Rules

If you start a new session from this plan:

1. Treat upstream `IXLandKernel` / `IXLandHostAdapter` as the architectural reference, not merely a naming reference.
2. Do not count a host-adapter refactor as successful if Linux semantics still live in app files.
3. Do not count a `rootfs` refactor as successful if Linux-facing behavior still depends on host-private policy.
4. Do not count a session fix as successful if it only works through a stale harness that the real app path does not use.
5. Use the vendored Linux tuple as the first contract source when refactoring Linux-owner code.
6. Do not claim merge-readiness unless the ownership split and the proof split are both real.

## Proof Gates

No tranche should be called complete without fresh proof.

Minimum proof categories:

- `xcodegen generate --project .` when `project.yml` changes
- simulator build proof on `iPhone 17`
- focused tests for the touched tranche
- full relevant suite before claiming a major migration step complete
- vendored-header compile-smoke proof whenever Linux-owner refactors change Linux header dependencies

For split-sensitive work, proof must answer these questions explicitly:

- was Linux semantics proved by kernel-facing tests?
- was host mediation proved by host-seam tests?
- did guest-visible behavior still work?
- did the TCTI-only path remain intact?
- did refactored code still resolve against the vendored Linux tuple rather than accidental local fallback definitions?

## Immediate Execution Order

If this plan is executed now, the required near-term order is:

1. keep this document and inventory true
2. finish hosted guest-session boundary decomposition
3. extract remaining Darwin host mechanics from `real.c` and `tty-real.c`
4. split `rootfs` Linux semantics from host persistence mechanics
5. shrink app-shell runtime ownership
6. express the local kernel / host-adapter target split in `project.yml`
7. collapse the test portfolio into canonical kernel, host-adapter, hosted-session, and E2E layers

This is the correct dependency order for the current repo, not the older fakefs-era order.

## Definition of Done

This alignment effort is done only when all of the following are true:

- `rootfs` is the live substrate, but its host-private role is narrow and mechanical
- Linux semantics are no longer decided inside `internal/ios/fs/rootfs.c` or other host-private files
- `real.c` and `tty-real.c` are no longer broad mixed Darwin/Linux owner seams
- hosted session startup ownership is clearly split across kernel-owner, host-adapter, and app-shell roles
- `project.yml` expresses a meaningful local kernel / host-adapter split or a clearly staged near-final equivalent
- runtime repro tests use only canonical harnesses aligned with the real app path
- app shell files no longer act as ambient runtime owners
- TCTI remains the only guest execution engine
- proof is split into kernel semantics, host-adapter seam, header compile-smoke, hosted-session, and app/E2E layers
- a future merge with upstream `IXLandKernel` / `IXLandHostAdapter` is blocked only by implementation depth, not by unresolved ownership shape
