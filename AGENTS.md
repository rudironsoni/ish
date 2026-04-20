Please also reference the following rules as needed. The list below is provided in TOON format, and `@` stands for the project root directory.

rules[5]{path}:
@.codex/memories/10-case-contract.md
@.codex/memories/20-instrumentation-lifecycle.md
@.codex/memories/30-runtime-reduction.md
@.codex/memories/40-tracing-logging.md
@.codex/memories/50-simulator-constraint.md

# 00-non-negotiables

These are repository-wide non-negotiable rules.

## Mandatory rules

1. No stub may count as pass.
2. No dynamic case discovery.
3. No hidden runtime dispatcher.
4. No direct edits to generated outputs.
5. No phase skipping.
6. No success-on-fallback behavior.
7. No case without explicit build wiring.
8. No vague completion claim without evidence review.
9. No later-phase progression while earlier gate cases are not `REAL PASS`.
10. No broad debugging while a smaller failing boundary can be reduced first.
11. No app bootstrap ownership outside app-owned `ISHInstrumentation`.
12. No direct investigation logging in product code.
13. No guest-runtime reintroduction before Task Zero is `REAL PASS`.
14. No broad observability work once runtime/kernel is the active blocker.

## Required status vocabulary

Use only:
- `REAL PASS`
- `REAL FAIL`
- `STUB`
- `BLOCKED`
- `INVALID`

## Repository posture

- Prefer smaller units.
- Prefer explicit contracts.
- Prefer deterministic fixtures.
- Prefer repo-local truth.
- Prefer exact failing edges over broad narratives.

## Instrumentation ownership

The canonical instrumentation system is `ISHInstrumentation`.

### Ownership model

- `main.m` MUST own minimal instrumentation bootstrap only.
- `AppDelegate` MUST own instrumentation activation.
- Lower layers MUST emit semantic events only.
- Lower layers MUST NOT own bootstrap, backend selection, recovery, persistence, or export policy.

### Product code isolation

- `kernel/*`, `app/*`, `emu/*`, and `tcti/*` MUST emit semantic instrumentation events only.
- Product code MUST NOT use direct `printk`, `NSLog`, `os_log`, or ad hoc logging for investigation.
- Product code MUST NOT own instrumentation bootstrap policy.
- Product code MUST NOT use constructor markers, proof files, startup path probes, or recovery files for investigation.

### Allowed producer path

Product code
→ semantic instrumentation API
→ app-owned instrumentation bridge
→ sink fanout
→ Apple logging / signposts / OpenTelemetry / MetricKit

## Control-plane MCPs

The control plane MUST treat only these as first-class MCPs:
- `XcodeBuildMCP`
- `GitHub`

---

# Build Truth

- `project.yml` is the canonical build configuration (XcodeGen source of truth)
- `IXLand.xcodeproj/project.pbxproj` is GENERATED OUTPUT — never hand-edit
- Versioned scheme `IXLandRuntime-6.12-arm64` is the target that MUST build clean

# Architecture

## Vendored Linux UAPI

- `third_party/linux-uapi/6.12/arm64/include/` contains the canonical Linux UAPI headers
- **NEVER edit vendored UAPI headers** — they are third-party truth
- Use `opencode.json` edit denial to prevent accidental modifications

## Type System

- `include/ixland/linux_types.h` is the narrow adapter: includes `<linux/types.h>` from vendored UAPI, maps underscore-suffixed ABI types (`pid_t_`, `uid_t_`, `mode_t_`, `off_t_`, `time_t_`, `clock_t_`)
- Do NOT create fake local std/UAPI header shims
- Use explicit casts for narrowing conversions — do NOT change function signatures

## Host-Bridge Quarantine (`internal/ios/`)

Darwin/iOS-specific code MUST be quarantined in `internal/ios/`. This includes:

1. **Socket Bridge** (`internal/ios/fs/sock_bridge.h/.c`)
   - Wraps Darwin POSIX socket functions (`socket`, `bind`, `connect`, `accept`, `getsockopt`, `setsockopt`, `sendmsg`, `recvmsg`, etc.)
   - Returns Linux-owner-compatible types with explicit casts at the boundary
   - Linux-owner code (`Sources/IXLandLinuxRuntime/fs/sock.c`) calls bridge functions, NOT direct Darwin APIs

2. **Log Bridge** (`internal/ios/kernel/log_bridge.h/.c`)
   - Wraps `os_log` APIs
   - Linux-owner code (`Sources/IXLandLinuxRuntime/kernel/log.c`) calls bridge functions, NOT `<os/log.h>` directly

3. **Platform Utilities** (`internal/ios/platform/`)
   - Darwin-specific: Mach time, sysctl, memory/CPU usage queries

**Rule**: Files in `Sources/IXLandLinuxRuntime/` MUST NOT directly include:
- `<sys/socket.h>`, `<netinet/tcp.h>`, `<sys/un.h>` (use `sock_bridge.h`)
- `<os/log.h>` (use `log_bridge.h`)
- `<mach/mach.h>`, `<sys/sysctl.h>` (use `platform/platform.h`)

This quarantine ensures:
- Clean separation between Linux-owner code and Darwin host specifics
- All narrowing conversions happen explicitly at the bridge boundary
- Portability to other host platforms in the future

# Macro Conventions

- `UNUSED(x)` expands to `UNUSED_##x __attribute__((unused))` — use in parameter declarations, NOT in function bodies
- `use(...)` expands to `__use(0, ##__VA_ARGS__)` — for consuming variadic args in trace macros
- `must_check` is `__attribute__((warn_unused_result))` — function annotation only
- Single definition site per macro — no duplicates across headers

# Code Style

- K&R empty parameter lists `()` are deprecated — use `(void)`
- Use `_Static_assert` for C11 static assertions (not `static_assert` without `<assert.h>`)
- Use `unsigned int` directly for bitfields (no `bitfield` macro)
- Use `container_of` from `util/misc.h` only (no duplicates)

# Testing

- No stub may count as pass
- No dynamic case discovery
- No success-on-fallback behavior
- Required status vocabulary: `REAL PASS`, `REAL FAIL`, `STUB`, `BLOCKED`, `INVALID`
