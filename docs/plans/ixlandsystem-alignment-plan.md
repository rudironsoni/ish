# IXLandKernel / IXLandHostAdapter Alignment Plan for `ish`

## Purpose

This document is the current migration plan for aligning this `ish` checkout with the refactored upstream `IXLandSystem` architecture on `main`.

The important upstream change is that `IXLandSystem` is no longer planned or described as one undifferentiated runtime tree. It is now explicitly split into:

- `IXLandKernel`
- `IXLandHostAdapter`
- `IXLandKernelTests`
- `IXLandHostAdapterTests`

That split changes what “alignment” means in this repo.

The goal is not to cosmetically imitate folder names. The goal is to reshape this `ish` checkout so that a future merge with `IXLandKernel` is technically plausible:

- Linux-shaped behavior must converge toward kernel-owned semantics.
- iOS and Darwin mediation must converge toward a host-adapter-owned boundary.
- app/UI code must stop acting as an ambient runtime substrate.
- test ownership must distinguish Linux semantics proof from host seam proof.
- TCTI-only guest execution must remain intact throughout the migration.

This plan is intentionally detailed so a fresh agent session can use it as an execution baseline without needing to rediscover the architecture from scratch.

## Why This Plan Changed

The earlier local plan assumed a simpler end-state:

- tuple-based Linux vendoring,
- a general `internal/ios` boundary,
- a synthetic VFS replacement,
- and eventual deletion of `fakefs` / `realfs` / `fake-db`.

That is still directionally correct, but it is no longer specific enough.

Upstream `IXLandSystem` now encodes stronger architectural rules:

- Linux semantics are owned by `IXLandKernel`, not by generic runtime code and not by host bridges.
- host mechanics are owned by `IXLandHostAdapter/internal/ios/**`, not sprinkled through kernel-owner paths.
- proof is split into Linux-facing kernel proof and private host-bridge proof.
- vendored Linux headers include both UAPI and kernel-header surfaces.
- XcodeGen and the generated Xcode project are the only authoritative build description.

Therefore this local plan must now optimize for future structural compatibility with a two-side split:

1. a future `ish` kernel side that can merge toward `IXLandKernel`
2. a future `ish` host mediation side that can merge toward `IXLandHostAdapter`

## Upstream Reference Snapshot

This section captures the relevant current upstream truth from `https://github.com/rudironsoni/IXLandSystem/tree/main`.

### Top-level split

Upstream currently has these top-level components:

- `IXLandKernel/`
- `IXLandHostAdapter/`
- `IXLandKernelTests/`
- `IXLandHostAdapterTests/`
- `project.yml`
- `IXLandKernel.xcodeproj/`
- `Makefile`
- `third_party/linux/...`

### Upstream kernel ownership

The Linux-owner side lives under `IXLandKernel/` and currently includes:

- `IXLandKernel/fs/`
- `IXLandKernel/kernel/`
- `IXLandKernel/runtime/`
- `IXLandKernel/include/`
- `IXLandKernel/internal/private/`
- `IXLandKernel/observability/`

Important current upstream kernel-owner surfaces include:

- `IXLandKernel/fs/vfs.c`
- `IXLandKernel/fs/fdtable.c`
- `IXLandKernel/fs/open.c`
- `IXLandKernel/fs/read_write.c`
- `IXLandKernel/fs/stat.c`
- `IXLandKernel/fs/fcntl.c`
- `IXLandKernel/fs/ioctl.c`
- `IXLandKernel/fs/namei.c`
- `IXLandKernel/fs/readdir.c`
- `IXLandKernel/fs/eventpoll.c`
- `IXLandKernel/fs/mount.c`
- `IXLandKernel/fs/inode.c`
- `IXLandKernel/fs/super.c`
- `IXLandKernel/fs/path.c`
- `IXLandKernel/fs/exec.c`
- `IXLandKernel/kernel/task.c`
- `IXLandKernel/kernel/fork.c`
- `IXLandKernel/kernel/exit.c`
- `IXLandKernel/kernel/wait.c`
- `IXLandKernel/kernel/pid.c`
- `IXLandKernel/kernel/cred.c`
- `IXLandKernel/kernel/signal.c`
- `IXLandKernel/kernel/time.c`
- `IXLandKernel/kernel/sync.c`
- `IXLandKernel/kernel/init.c`
- `IXLandKernel/kernel/sys.c`
- `IXLandKernel/kernel/resource.c`
- `IXLandKernel/kernel/random.c`
- `IXLandKernel/runtime/syscall.c`
- `IXLandKernel/runtime/native/registry.c`

### Upstream host-adapter ownership

The host-mechanics side lives under `IXLandHostAdapter/` and currently includes:

- `IXLandHostAdapter/include/`
- `IXLandHostAdapter/internal/ios/fs/`
- `IXLandHostAdapter/internal/ios/kernel/`
- `IXLandHostAdapter/internal/ios/runtime/`

Important current upstream host-adapter files include:

- `IXLandHostAdapter/internal/ios/fs/backing_io.m`
- `IXLandHostAdapter/internal/ios/fs/backing_paths.m`
- `IXLandHostAdapter/internal/ios/fs/path_host.c`
- `IXLandHostAdapter/internal/ios/fs/errno_host.c`
- `IXLandHostAdapter/internal/ios/fs/open_flags.c`
- `IXLandHostAdapter/internal/ios/fs/memfd_host.c`
- `IXLandHostAdapter/internal/ios/fs/epoll_bridge.c`
- `IXLandHostAdapter/internal/ios/fs/sync.c`
- `IXLandHostAdapter/internal/ios/kernel/signal_bridge.c`
- `IXLandHostAdapter/internal/ios/kernel/clock.c`
- `IXLandHostAdapter/internal/ios/kernel/sync.c`
- `IXLandHostAdapter/internal/ios/runtime/sync.h`

### Upstream build and header truth

Upstream `project.yml` currently encodes:

- target `IXLandKernel`
- target `IXLandHostAdapter`
- target `IXLandKernelTests`
- target `IXLandHostAdapterTests`
- scheme `IXLandKernel-6.12-arm64`
- tuple-root Linux variables:
  - `LINUX_VENDOR_ROOT`
  - `LINUX_ROOT`
  - `LINUX_UAPI_ROOT`
  - `LINUX_UAPI_INCLUDE_ROOT`
  - `LINUX_KHEADERS_ROOT`
  - `LINUX_KHEADERS_SOURCE_ROOT`
  - `LINUX_KHEADERS_GENERATED_ROOT`

Upstream `Makefile` currently vendors:

- `third_party/linux/<version>/<arch>/uapi/include`
- `third_party/linux/<version>/<arch>/kheaders/source`
- `third_party/linux/<version>/<arch>/kheaders/generated`
- `source.json`
- `README.md`
- `manifest.sha256`

This is more specific than the older local assumption that tuple vendoring only needed `uapi`, `srctree`, and `objtree`.

For future mergeability, this repo must plan against the current upstream vendoring shape, not an older snapshot.

### Upstream proof split

Upstream now distinguishes:

1. `IXLandKernelTests`
   Linux-facing kernel semantics proof.

2. `IXLandHostAdapterTests`
   private iOS host-seam proof.

3. Linux header compile-smoke coverage
   header resolution proof, not runtime proof.

This test split is not incidental. It is part of the architecture and must inform the local migration.

## Local Repo Baseline

This `ish` checkout is not yet split that way.

### Current kernel-adjacent local surfaces

The closest current local analogue to a future `IXLandKernel` side is:

- `Sources/IXLandLinuxRuntime/fs/`
- `Sources/IXLandLinuxRuntime/kernel/`
- `Sources/IXLandLinuxRuntime/emu/`
- `Sources/IXLandLinuxRuntime/tcti/`
- `Sources/IXLandLinuxRuntime/include/`
- `Sources/IXLandLinuxRuntime/util/`

These paths currently mix several concerns:

- Linux-facing semantics
- runtime ABI / syscall dispatch
- guest execution substrate
- legacy fakefs / realfs / fake-db behavior
- some host leakage and historical iSH-shaped assumptions

### Current host-adapter-adjacent local surfaces

The closest current local analogue to a future `IXLandHostAdapter` side is split across:

- `internal/ios/fs/`
- `internal/ios/kernel/`
- `internal/ios/platform/`
- app-owned runtime integration in `Sources/IXLandTerminal/`

Important app/runtime files that currently carry host or bootstrap responsibilities include:

- `Sources/IXLandTerminal/AppDelegate.m`
- `Sources/IXLandTerminal/Roots.m`
- `Sources/IXLandTerminal/iOSFS.m`
- `Sources/IXLandTerminal/LinuxRoot.c`
- `Sources/IXLandTerminal/LinuxPTY.c`
- `Sources/IXLandTerminal/LinuxTTY.c`
- `Sources/IXLandTerminal/GhosttyHostTerminal.swift`
- `Sources/IXLandTerminal/Terminal.m`

This means host mediation is not only in `internal/ios/**`; it is also ambient in the app target today.

### Current local build and header state

Current local `project.yml` and vendoring layout now have partial upstream parity:

- they point at `third_party/linux/<version>/<arch>`
- they define `LINUX_KHEADERS_*` variables alongside the UAPI tuple variables
- they still compile legacy fakefs helper sources into the terminal app
- they still include `internal/ios` directly rather than via a dedicated host-adapter target

### Current local legacy blockers

Major live blockers to future mergeability include:

- `Sources/IXLandLinuxRuntime/fs/fake.c`
- `Sources/IXLandLinuxRuntime/fs/real.c`
- `Sources/IXLandLinuxRuntime/fs/fake-db.c`
- `Sources/IXLandLinuxRuntime/fs/fake-rebuild.c`
- `Sources/IXLandLinuxRuntime/fs/fake-migrate.c`
- `Sources/IXLandLinuxRuntime/kernel/fs.h` fakefs ownership
- `Sources/IXLandLinuxRuntime/kernel/xX_main_Xx.h` fakefs/realfs selection
- `Sources/IXLandTerminal/iOSFS.m` direct realfs plumbing
- `Sources/IXLandTerminal/Roots.m` fakefs import/export plumbing
- `Sources/IXLandTerminal/AppDelegate.m` fakefs-root bootstrap assumptions

## Alignment Objective

The objective is to transform this repo from:

- a monolithic `IXLandLinuxRuntime` plus app-owned bootstrap and host behavior

into something that can later converge with:

- `IXLandKernel` for Linux-owner semantics
- `IXLandHostAdapter` for host mediation

without abandoning the following local invariants:

- TCTI-only guest AArch64 execution
- guest PTY semantics
- one active guest session
- iPhone 17 simulator default
- `project.yml` as build truth
- upstream Swift Package use for `libarchive-for-swift` and `libghostty-spm`

## Core Migration Thesis

The correct long-term decomposition for this repo is:

1. Linux-owner runtime side
   This is the future-mergeable side that should converge toward `IXLandKernel`.

2. host-adapter side
   This is the private iOS mediation side that should converge toward `IXLandHostAdapter`.

3. app shell side
   This remains app/UI/bootstrap territory and should shrink, not expand, as a runtime owner.

The migration is successful when runtime behavior stops depending on ambient app files and instead flows through a kernel/host-adapter split.

## Non-Negotiable Constraints

- Guest AArch64 execution remains TCTI-only.
- Do not add or revive a fallback interpreter or alternate guest CPU path.
- Linux-facing behavior must not be defined by Darwin semantics.
- Host mechanics must be explicit and private.
- Linux-owner refactoring must use the vendored Linux header tuple as the contract source:
  - `third_party/linux/<version>/<arch>/uapi/include` for UAPI-facing work
  - `third_party/linux/<version>/<arch>/kheaders/source` and `kheaders/generated` for kernel-header-facing work
- `project.yml` remains the authoritative build specification.
- iPhone 17 remains the default simulator target for proof.
- `/Volumes/1TB/Xcode/DerivedData` remains the default DerivedData/build location.
- plan work must optimize for future mergeability with upstream `IXLandKernel`, not just local cleanliness.

## Architectural Mapping for This Repo

This section tells a fresh agent how to think about the current tree during refactoring.

### Proto-kernel side in this repo

Treat these paths as the local proto-`IXLandKernel` side:

- `Sources/IXLandLinuxRuntime/fs/`
- `Sources/IXLandLinuxRuntime/kernel/`
- `Sources/IXLandLinuxRuntime/include/`
- `Sources/IXLandLinuxRuntime/util/`
- the Linux-facing part of `Sources/IXLandLinuxRuntime/tcti/`
- the runtime ABI and guest execution entry surfaces in `Sources/IXLandLinuxRuntime/emu/`

These paths should move toward:

- Linux-shaped VFS ownership
- Linux-shaped process/task/signal/credential semantics
- Linux-owned syscall/runtime dispatch
- Linux-owned PTY/job-control semantics
- vendored Linux header truth
- Linux include forms and constants derived from the vendored tuple instead of ad hoc local copies or Darwin substitutes

They should move away from:

- direct host APIs
- app-owned bootstrap logic
- fakefs-root assumptions
- generic “helper” escape hatches that smuggle host behavior into kernel-owner code

### Proto-host-adapter side in this repo

Treat these paths as the local proto-`IXLandHostAdapter` side:

- `internal/ios/fs/`
- `internal/ios/kernel/`
- `internal/ios/platform/`
- host-only shims that should be moved out of `Sources/IXLandTerminal/`

The target direction is:

- private host path discovery
- host errno translation
- host-backed IO and storage mediation
- host clock/sync/signal bridge mechanics
- host-only PTY/file-system support seams where the kernel side needs narrow mediation

### App shell side in this repo

Treat `Sources/IXLandTerminal/` as app shell, not kernel owner and not generic host adapter.

Its responsibilities should shrink toward:

- UI and terminal presentation
- lifecycle orchestration
- rootfs packaging/user flows
- wiring the app to a kernel/host-adapter substrate

It should stop owning:

- fakefs-root semantic policy
- realfs semantic policy
- ambient host path translation
- Linux-facing VFS rules
- guest runtime semantics

## Vendored Header Rule For Refactoring

This repo now has the current tuple-root Linux vendor shape locally:

- `third_party/linux/6.12/arm64/uapi/include`
- `third_party/linux/6.12/arm64/kheaders/source`
- `third_party/linux/6.12/arm64/kheaders/generated`

That must actively shape refactoring work.

Fresh agents must use the vendored tuple in these ways:

1. When moving Linux-owner code toward upstream `IXLandKernel`, prefer Linux include forms and Linux-defined constants/types/macros from the vendored tuple over local hand-maintained stand-ins.
2. When deciding whether a definition belongs to kernel-owner code or host-adapter code, ask whether the interface is Linux-defined and already present in the vendored tuple. If yes, the default assumption should be kernel-owner unless there is a narrow host-mechanics reason otherwise.
3. Do not introduce new local compatibility headers, copied constant blocks, or Darwin-derived replacements when the vendored tuple already provides the contract surface.
4. When merging or reshaping files to resemble upstream `IXLandKernel`, preserve upstream-style include direction as much as possible so later file-level merges remain low-noise.
5. If a refactor needs kernel-header resolution, wire the code and tests against `LINUX_KHEADERS_SOURCE_ROOT` and `LINUX_KHEADERS_GENERATED_ROOT` rather than inventing alternative include roots.

This does not mean every file should directly include deep vendored paths.
It means the vendored Linux tuple is the source of truth that refactoring decisions must honor.

## Anti-Goals

This plan explicitly rejects the following bad migration patterns:

- renaming folders to `IXLandKernel` / `IXLandHostAdapter` before behavior and ownership are corrected
- moving code into `internal/ios/**` without narrowing ownership
- leaving Linux semantics in app files while claiming “host adapter introduced”
- preserving fakefs/realfs/fake-db as hidden substrate under a new name
- using host-adapter tests as proof that Linux semantics are correct
- weakening TCTI constraints to make restructuring easier
- broad “cleanup” refactors that do not change mergeability

## Execution Tranches

The tranches below are intentionally ordered by dependency and merge value.

### Tranche 0: Plan and inventory normalization

Objective:
Make the local plan accurately describe the post-split upstream architecture and the local mismatch against it.

Required work:

1. Keep this document synced to upstream `IXLandKernel` / `IXLandHostAdapter` structure.
2. Maintain a concrete inventory of:
   - current proto-kernel paths
   - current proto-host-adapter paths
   - current app-owned runtime paths
3. Identify every live reference to:
   - legacy Linux vendoring assumptions that predate `third_party/linux/<version>/<arch>`
   - `fakefs`
   - `realfs`
   - `fake-db`
   - app-owned host mediation
4. Prevent future migration work from assuming the split already exists locally.

Acceptance:

- The plan describes repo truth.
- A fresh agent can derive ownership intent from this document alone.

### Tranche 1: Linux vendoring and build-surface parity

Objective:
Align local build/header infrastructure with the current upstream `IXLandKernel` build surface.

Required work:

1. Keep local vendoring aligned to upstream-style tuple-root variables:
   - `LINUX_VENDOR_ROOT`
   - `LINUX_ROOT`
   - `LINUX_UAPI_ROOT`
   - `LINUX_UAPI_INCLUDE_ROOT`
   - `LINUX_KHEADERS_ROOT`
   - `LINUX_KHEADERS_SOURCE_ROOT`
   - `LINUX_KHEADERS_GENERATED_ROOT`
2. Port the current upstream `vendor-linux-headers` behavior, not an older approximation.
3. Vendor all required surfaces under:
   - `third_party/linux/<version>/<arch>/uapi/include`
   - `third_party/linux/<version>/<arch>/kheaders/source`
   - `third_party/linux/<version>/<arch>/kheaders/generated`
4. Carry upstream metadata outputs:
   - `source.json`
   - `README.md`
   - `manifest.sha256`
5. Update local compile-smoke coverage so it can prove:
   - UAPI resolution
   - kernel-header resolution where required
6. Keep include forms Linux-shaped and ban direct tuple-path includes in code.
7. Treat the vendored tuple as the canonical source for Linux constants, structs, and macros used during refactors; remove duplicated local definitions when the tuple already covers them.

Why this matters for future merge:

- Upstream kernel-owner code assumes this header model.
- Without vendoring parity, later file-level merges will be noisy and misleading.

Acceptance:

- local `project.yml` uses upstream-style Linux vendor variables
- vendoring is repo-owned and deterministic
- compile-smoke proof covers both UAPI and any required kernel-header surfaces

### Tranche 2: Introduce explicit local split boundaries

Objective:
Create a local structural boundary that mirrors upstream’s kernel side versus host-adapter side, even if names are not yet fully cut over.

Required work:

1. Define the local proto-kernel boundary explicitly around Linux-owner code.
2. Define the local proto-host-adapter boundary explicitly around host-only mediation.
3. Stop treating `internal/ios` as “misc platform utilities”; it must become host-adapter territory.
4. Introduce or refine narrow bridge contracts between the Linux side and host side.
5. Ensure those bridge contracts are private and subsystem-specific, not generic global adapters.
6. For every boundary introduced here, verify that Linux-defined types and constants stay on the kernel-owner side via the vendored headers instead of being redefined in host-adapter or app code.

Key local pressure points:

- `Sources/IXLandLinuxRuntime/kernel/`
- `Sources/IXLandLinuxRuntime/fs/`
- `internal/ios/fs/`
- `internal/ios/kernel/`
- `Sources/IXLandTerminal/iOSFS.m`
- `Sources/IXLandTerminal/LinuxPTY.c`
- `Sources/IXLandTerminal/LinuxTTY.c`

Acceptance:

- a reviewer can point to what is kernel-owner, host-adapter-owner, and app-owner
- new changes have a clear home instead of expanding ambient app/runtime coupling

### Tranche 3: Evict host behavior from Linux-owner paths

Objective:
Move host mechanics out of Linux-owner code and into the local proto-host-adapter side.

Required work:

1. Audit Linux-owner files for Darwin/iOS-specific behavior and host assumptions.
2. Move host path discovery, errno translation, backing storage, timing, signal bridge, and sync mechanics behind dedicated host-adapter seams.
3. Replace direct or ambient host coupling with kernel-owned contracts plus host-adapter implementations.
4. Preserve semantics while moving mechanics.
5. As Linux-owner files are touched, normalize them toward vendored-header-backed contracts instead of preserving stale local stand-ins for Linux types or flags.

Important nuance:

This tranche is not “just move files.”
The value is restoring directionality:

- Linux-owner side decides Linux semantics.
- host-adapter side performs private host work.

Acceptance:

- Linux-owner files no longer define behavior by reaching into host APIs or app-owned helpers
- host adapter owns mechanics, not semantics

### Tranche 4: App shell decontamination

Objective:
Shrink `Sources/IXLandTerminal/` so it stops being a hidden substrate layer.

Required work:

1. Identify all app files currently making runtime-semantic decisions.
2. Move runtime-semantic ownership out of:
   - `AppDelegate.m`
   - `Roots.m`
   - `iOSFS.m`
   - `LinuxRoot.c`
   - `LinuxPTY.c`
   - `LinuxTTY.c`
3. Keep terminal UI and orchestration in the app target, but move kernel/host-adapter policy into the correct side.
4. Preserve bundled rootfs and archive flows only where they are truly app-owned.

Why this matters for mergeability:

Upstream `IXLandKernel` and `IXLandHostAdapter` are libraries with clear ownership.
If this repo leaves runtime behavior trapped in app glue, future merge work will stall at the wrong seam.

Acceptance:

- app target is visibly thinner as a runtime owner
- app shell wires components together instead of defining Linux semantics

### Tranche 5: Replace fakefs / realfs / fake-db with kernel-owned VFS plus host-adapter backing

Objective:
Delete the old substrate model and replace it with a split-compatible one.

Required work:

1. Define the local VFS replacement in kernel-owner terms:
   - mount lifecycle
   - inode ownership
   - path traversal
   - fdtable integration
   - readdir/stat/open/read/write/fcntl/ioctl/exec hooks
   - PTY/job-control/readiness integration
2. Back that VFS with host-adapter-owned private host mediation.
3. Stop treating `realfs` as the underlying semantics owner.
4. Stop treating `fakefs` metadata as the primary root model.
5. Remove `fake-db` ownership from kernel-facing data structures.
6. Use vendored Linux headers as the semantic contract for VFS-visible structs, flags, ioctls, and syscall-adjacent definitions rather than local approximations.

This is the key merge-preparation tranche.

Upstream `IXLandKernel/fs/**` assumes kernel-owned VFS semantics.
As long as this repo remains fakefs/realfs-shaped, future merging will be architectural churn, not integration.

Acceptance:

- kernel-owner VFS path exists and is used for real boot/runtime flows
- fakefs/realfs/fake-db no longer define the substrate contract

### Tranche 6: TCTI and runtime ownership convergence

Objective:
Keep the guest execution path aligned with the split instead of letting emulation become a parallel architecture.

Required work:

1. Keep TCTI as the only guest AArch64 execution engine.
2. Clarify which parts of local `emu/` and `tcti/` belong to future kernel/runtime ownership.
3. Ensure syscall exits, runtime ABI, PTY/session wiring, and guest state transitions flow through kernel-owned logic plus host-adapter seams.
4. Prevent app glue or host-adapter code from becoming an alternate execution-control plane.

Why this matters:

Future mergeability is not only about file systems and headers.
It also requires the guest runtime path to remain conceptually kernel-owned.

Acceptance:

- TCTI path remains authoritative
- runtime transitions are not smeared across app and host files

### Tranche 7: Local proof split mirroring upstream

Objective:
Split local proof into kernel-facing semantics proof versus host-seam proof.

Required work:

1. Introduce or reorganize tests so Linux-facing semantics are proved separately from host bridge mechanics.
2. Treat host-seam tests as required for repo green, but not as substitute proof for Linux semantics.
3. Add compile-smoke coverage for the new vendored header model.
4. Preserve focused terminal and runtime regressions for guest-visible behavior.
5. Add or maintain targeted compile-smoke checks when refactors start depending on vendored kernel-header surfaces, not only UAPI surfaces.

Local test taxonomy target:

1. kernel semantics proof
2. host-adapter seam proof
3. vendored header compile smoke
4. app-path end-to-end proof

Acceptance:

- a failing test can be classified immediately as kernel proof, host-adapter proof, compile-smoke proof, or app/E2E proof
- reviewers can see which layer a claimed fix actually proved

### Tranche 8: Naming and structural cutover toward upstream shapes

Objective:
After behavior and boundaries are corrected, reduce structural drift from upstream names and module layout.

Required work:

1. Reorganize local source roots so future merges with `IXLandKernel` and `IXLandHostAdapter` become less intrusive.
2. Move local include and private-contract surfaces toward upstream-style layout.
3. Update `project.yml` targets and source groups to reflect the split.
4. Keep compatibility shims minimal and temporary.

Important rule:

This tranche comes after ownership correction, not before.
Premature renaming without behavioral alignment is churn.

Acceptance:

- local source layout no longer fights the upstream split
- future file-level merge planning becomes straightforward instead of speculative

## Fresh-Agent Operating Rules

If you start a new session from this plan, follow these rules:

1. Treat upstream `IXLandKernel` / `IXLandHostAdapter` as the architectural reference, not merely a naming reference.
2. Do not count a host-adapter refactor as successful if Linux semantics still live in app files.
3. Do not count a VFS refactor as successful if `fakefs` / `realfs` still define the actual substrate.
4. Do not count a test split as successful if host tests are still being cited as Linux proof.
5. Use the vendored Linux tuple as the first contract source when refactoring Linux-owner code; do not invent parallel local header truth without necessity.
6. Do not claim merge-readiness unless both the ownership split and the behavior split are real.

## Proof Gates

No tranche should be called complete without fresh proof.

Minimum proof categories are:

- `xcodegen generate --project .` when `project.yml` changes
- simulator build proof on iPhone 17
- focused tests for the touched tranche
- full relevant suite before claiming a major migration step complete
- vendored header compile-smoke proof whenever Linux-owner refactors add or change Linux header dependencies

For split-sensitive work, proof must answer these questions explicitly:

- Was Linux semantics proved by kernel-facing tests?
- Was host mediation proved by host-seam tests?
- Did guest-visible behavior still work?
- Did the TCTI-only path remain intact?
- Did the refactor still resolve against the vendored Linux tuple rather than against accidental local fallback definitions?

## Immediate Execution Order

If this plan is executed now, the recommended near-term order is:

1. finish plan/inventory truth
2. keep local Linux vendoring and build variables aligned with current upstream and use that tuple as the contract source for subsequent refactors
3. establish explicit local proto-kernel versus proto-host-adapter boundaries
4. evict host mechanics from Linux-owner code
5. decontaminate app-owned runtime logic
6. replace fakefs/realfs/fake-db substrate ownership
7. split proof layers
8. only then perform deeper naming/layout cutover

## Definition of Done

This alignment effort is done only when all of the following are true:

- local build and header infrastructure matches the current upstream `IXLandKernel` model closely enough for low-friction future merge work
- Linux-owner behavior in this repo has a clear home that is converging toward `IXLandKernel`
- private iOS/Darwin mediation in this repo has a clear home that is converging toward `IXLandHostAdapter`
- app shell files are no longer acting as ambient runtime owners
- fakefs/realfs/fake-db no longer define the substrate contract
- TCTI remains the only guest execution engine
- proof is split into kernel semantics, host-adapter seam, header compile-smoke, and app/E2E layers
- a future merge with upstream `IXLandKernel` is blocked only by remaining implementation gaps, not by unresolved architecture shape confusion
