12-rule template

These rules apply to every task in this project unless explicitly overridden.
Bias: caution over speed on non-trivial work. Use judgment on trivial tasks.

## Rule 1 — Think Before Coding
State assumptions explicitly. If uncertain, ask rather than guess.
Present multiple interpretations when ambiguity exists.
Push back when a simpler approach exists.
Stop when confused. Name what's unclear.

## Rule 2 — Simplicity First
Minimum code that solves the problem. Nothing speculative.
No features beyond what was asked. No abstractions for single-use code.
Test: would a senior engineer say this is overcomplicated? If yes, simplify.

## Rule 3 — Surgical Changes
Touch only what you must. Clean up only your own mess.
Don't "improve" adjacent code, comments, or formatting.
Don't refactor what isn't broken. Match existing style.

## Rule 4 — Goal-Driven Execution
Define success criteria. Loop until verified.
Don't follow steps. Define success and iterate.
Strong success criteria let you loop independently.

## Rule 5 — Use the model only for judgment calls
Use me for: classification, drafting, summarization, extraction.
Do NOT use me for: routing, retries, deterministic transforms.
If code can answer, code answers.

## Rule 6 — Token budgets are not advisory
Per-task: 4,000 tokens. Per-session: 30,000 tokens.
If approaching budget, summarize and start fresh.
Surface the breach. Do not silently overrun.

## Rule 7 — Surface conflicts, don't average them
If two patterns contradict, pick one (more recent / more tested).
Explain why. Flag the other for cleanup.
Don't blend conflicting patterns.

## Rule 8 — Read before you write
Before adding code, read exports, immediate callers, shared utilities.
"Looks orthogonal" is dangerous. If unsure why code is structured a way, ask.

## Rule 9 — Tests verify intent, not just behavior
Tests must encode WHY behavior matters, not just WHAT it does.
A test that can't fail when business logic changes is wrong.

## Rule 10 — Checkpoint after every significant step
Summarize what was done, what's verified, what's left.
Don't continue from a state you can't describe back.
If you lose track, stop and restate.

## Rule 11 — Match the codebase's conventions, even if you disagree
Conformance > taste inside the codebase.
If you genuinely think a convention is harmful, surface it. Don't fork silently.

## Rule 12 — Fail loud
"Completed" is wrong if anything was skipped silently.
"Tests pass" is wrong if any were skipped.
Default to surfacing uncertainty, not hiding it.

# AGENTS.md - `ish` Alignment and Runtime Rules

## Mission

This repository is being reshaped so it can eventually merge cleanly with the current upstream `IXLandSystem` architecture, especially:

- `IXLandKernel`
- `IXLandHostAdapter`

Work in this repo must optimize for that future merge.

If a local change makes future convergence with upstream `IXLandKernel` / `IXLandHostAdapter` harder, the change is wrong unless the user explicitly approves that tradeoff.

## Hard Rule: Always Check Upstream Structure First

Before making architecture, layout, ownership, build-surface, vendoring, runtime-boundary, or test-layering decisions, always inspect the current upstream repository:

- `https://github.com/rudironsoni/IXLandSystem`

Do not rely on older memory, older docs, or guessed structure when shaping this repo.

You must look at the current upstream tree, build files, and relevant docs to mimic the structure and patterns implemented there wherever this repo is intended to converge.

This is a hard rule for:

- source tree organization
- kernel versus host-adapter ownership
- `project.yml` structure
- vendored Linux header layout
- test target layering
- private bridge patterns
- naming and file placement

Do not invent a local architecture if upstream already expresses the intended shape.

## Canonical Local Plan

The authoritative local migration plan is:

- `docs/plans/ixlandsystem-alignment-plan.md`

Treat that file as the repo-local execution baseline.
Keep it aligned with current upstream `IXLandSystem` structure and current local repo truth.

## Core Architecture Direction

This repo is not aiming for generic cleanup.
It is aiming for a future split-compatible architecture:

1. a Linux-owner side converging toward `IXLandKernel`
2. a host-mediation side converging toward `IXLandHostAdapter`
3. an app shell side that stops acting as an ambient runtime substrate

### Local proto-kernel side

Treat these local areas as the proto-`IXLandKernel` side:

- `Sources/IXLandLinuxRuntime/fs/`
- `Sources/IXLandLinuxRuntime/kernel/`
- `Sources/IXLandLinuxRuntime/include/`
- `Sources/IXLandLinuxRuntime/util/`
- Linux-facing parts of `Sources/IXLandLinuxRuntime/emu/`
- Linux-facing parts of `Sources/IXLandLinuxRuntime/tcti/`

These paths should converge toward:

- Linux-shaped semantics
- kernel-owned VFS and fdtable behavior
- kernel-owned syscall/runtime dispatch
- kernel-owned PTY/job-control behavior
- vendored Linux header truth

### Local proto-host-adapter side

Treat these local areas as the proto-`IXLandHostAdapter` side:

- `internal/ios/fs/`
- `internal/ios/kernel/`
- `internal/ios/platform/`
- host-only runtime seams that should be extracted from app files

These paths should own host mechanics only:

- backing storage and path mediation
- errno translation
- host timing and sync mechanics
- host signal bridge mechanics
- other narrow subsystem-specific iOS/Darwin bridges

### App shell side

Treat `Sources/IXLandTerminal/` as app shell.

It may own:

- UI
- lifecycle orchestration
- terminal presentation
- user flows
- rootfs packaging/import/export orchestration

It should not own Linux semantics or become a general runtime substrate.

## Hard Constraints: Guest Emulation

- TCTI is the required AArch64 guest emulation engine.
- Use TCTI to its fullest for Linux guest execution on iOS under Apple App Store constraints against JIT.
- Do not add, restore, or route guest AArch64 execution through a separate interpreter, fallback interpreter, or ad hoc instruction execution path when that behavior can be implemented in TCTI.
- Missing instruction semantics, fault handling, syscall exits, signals, dispatch, state flush, or guest architectural behavior must be advanced through TCTI and its documented contracts.
- Host-side scaffolding may support loading, scheduling, PTY/session wiring, tracing, and tests, but must not become an alternate guest CPU execution engine.
- Linux mechanisms such as ELF `PT_INTERP`, dynamic linker execution, shebang handling, and shell commands are allowed only as guest code executing through TCTI.
- Terminal/runtime work must preserve guest-backed PTY semantics and must not revert to host-side fake PTY behavior.
- Keep Linux guest startup bounded to one active guest emulator/session at a time.

## Hard Constraints: Linux-Shaped Behavior

- Linux-facing behavior must not drift toward Darwin-shaped semantics just because Darwin behavior is convenient.
- Kernel-owner code should decide Linux semantics.
- Host-adapter code should perform private host mechanics.
- App code should wire components together, not define runtime contracts.
- Do not use host-adapter tests as substitute proof that Linux semantics are correct.

## Hard Constraints: Project Integration

- `project.yml` is the source of truth for generated Xcode project structure and package integration.
- Use iPhone 17 as the default simulator destination for local verification.
- Use `/Volumes/1TB/Xcode/DerivedData` for Xcode DerivedData and build artifacts.
- Use `/Volumes/1TB/Xcode/Caches` for Xcode-related caches.
- Preserve `libarchive` support for root archive import/export operations.
- Use `libarchive-for-swift` exclusively as an upstream Swift Package dependency. Do not replace it with local vendored copies, wrappers, checkouts, forks, or local edits.
- Use `libghostty-spm` exclusively as an upstream Swift Package dependency. Do not replace it with local vendored copies, wrappers, checkouts, forks, or local edits.

## Hard Constraints: Linux Header Truth

Upstream now treats vendored Linux headers as generated truth, not hand-authored local approximations.

When aligning this repo, prefer the current upstream `IXLandSystem` vendoring model:

- `third_party/linux/<version>/<arch>/uapi/include`
- `third_party/linux/<version>/<arch>/kheaders/source`
- `third_party/linux/<version>/<arch>/kheaders/generated`

Do not preserve `third_party/linux-uapi` just because it already exists locally.

Do not hand-author Linux-looking headers when the upstream vendoring pipeline is supposed to generate the authoritative surface.

## Ownership Rules

### Linux-owner code

In Linux-owner paths:

- prefer Linux-shaped names, contracts, constants, and semantics
- keep syscall-facing ownership in the owning subsystem
- keep VFS behavior kernel-owned
- keep PTY, job control, signal, credential, and task semantics Linux-owned

Do not:

- pull Darwin/iOS semantics directly into Linux-owner code
- define runtime behavior in app glue
- solve boundary mistakes with generic helper bags
- preserve fakefs/realfs/fake-db ownership under a new name

### Host-adapter code

In host-adapter paths:

- keep seams narrow
- make them subsystem-specific
- make them private
- let them implement mechanics, not policy

Do not:

- move Linux semantic decisions into host-adapter code
- create giant catch-all bridge layers
- use host vocabulary as a substitute public contract

### App shell code

In app shell paths:

- keep runtime ownership minimal
- avoid ambient filesystem and process policy
- move substrate logic out when it becomes reusable or semantic

## Legacy Substrate Rule

`fakefs`, `realfs`, and `fake-db` are legacy substrate components, not target architecture.

Do not build new features that deepen dependence on:

- `Sources/IXLandLinuxRuntime/fs/fake*`
- `Sources/IXLandLinuxRuntime/fs/real*`
- `Sources/IXLandLinuxRuntime/fs/fake-db*`
- app-owned fakefs-root conventions

If you touch those areas, bias toward migration, containment, or replacement, not reinforcement.

## Testing and Proof Layering

When shaping tests, mirror the upstream split in intent:

1. Linux-facing kernel semantics proof
2. private host-adapter seam proof
3. Linux header compile-smoke proof
4. app-path and guest E2E proof

Rules:

- Linux semantics must be proved by Linux-facing runtime tests.
- Host bridge tests prove host seams only.
- Compile-smoke tests prove header resolution, not runtime semantics.
- Guest-visible terminal/runtime regressions are still required where relevant.
- Contract and TCTI tests are enforcement tools, not the destination. They must isolate emulator/runtime defects, protect architectural contracts, and prevent regressions, but they do not by themselves define product progress.
- Guest-runtime system proof and app/E2E proof are the north star. Choose work based on the user-visible runtime path first, then add or tighten the contract coverage needed to make that progress robust.
- Runtime work streams should normally close with both layers of proof:
  1. a focused contract/runtime test that isolates the exact defect or invariant
  2. a relevant guest/system or app/E2E proof that shows the fix matters in the real product path
- Do not treat a green contract suite alone as sufficient evidence that the app, guest runtime, or overall product meaningfully improved.
- Runtime fixes must be advanced by tests first: add or tighten a failing test that captures the exact user-visible or contract-visible failure, verify it fails for the expected reason, then implement the fix.
- Do not use trial-and-error patching as a debugging method. Instrumentation is allowed only to sharpen the failing contract and isolate root cause; it is not a substitute for a targeted red test.
- When multiple red tests exist, the next code change must be justified against a specific failing contract, not a vague performance suspicion.
- If a fix lands without making a targeted failing test pass or otherwise narrowing the exact failing contract, the work is incomplete.

## Tooling Rules

- Always prefix shell commands with `rtk`. See `@/Users/rudironsoni/.codex/RTK.md`.
- For iOS, simulator, test, run, log, debug, and UI-automation work, use the `xcodebuildmcp-cli` skill first:
  - `/Users/rudironsoni/.codex/skills/xcodebuildmcp-cli/SKILL.md`
- Prefer `xcodebuildmcp` workflows and tools over raw `xcodebuild`, `xcrun`, or `simctl`.
- Use `xcodebuildmcp --help`, `xcodebuildmcp tools`, and per-workflow `--help` discovery before running build/test/run actions.
- Keep `xcodebuildmcp` execution aligned with local defaults:
  - simulator: `iPhone 17`
  - DerivedData: `/Volumes/1TB/Xcode/DerivedData`
  - caches: `/Volumes/1TB/Xcode/Caches`
- Only fall back to raw `xcodebuild`, `xcrun`, or `simctl` when `xcodebuildmcp` cannot perform the required action or the user explicitly requests the fallback.
- Use raw `git` for status, commits, and pushes.

## Quality Bar

Forbidden:

- architecture changes made without checking current upstream `IXLandSystem`
- cosmetic folder renames presented as real alignment
- generic platform helpers that blur kernel versus host-adapter ownership
- shallow compatibility stubs presented as real substrate progress
- false completion claims without current proof
- preserving ambient app-owned runtime logic because it is locally convenient

Required:

- use current upstream repo truth as the structural reference
- keep local docs aligned with repo truth
- move toward explicit ownership, not broader convenience layers
- preserve TCTI-only guest execution
- prove behavior before claiming success
- treat app and guest-runtime progress as the holy-grail north star, with contract tests serving that goal instead of replacing it
- for stage, milestone, or work-stream completion, prefer proof that includes both the contract layer and a relevant guest/system or app/E2E layer
- use failing tests and exact proof to drive emulator/runtime fixes from root cause, not exploratory patch sequences
