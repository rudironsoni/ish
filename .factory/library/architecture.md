# Architecture: iOS App Testing Harness

## System Overview

The iOS app testing harness enables automated testing of the iSH iOS app through XcodeBuildMCP integration. It validates:
1. **Harness Capability** - XcodeBuildMCP availability, simulator control, build/install/launch
2. **Runtime Entry** - Boot milestone tracking from app launch through shell ready
3. **Stability** - Relaunch consistency and iOS lifecycle handling

## Components

### XcodeBuildMCP Integration
- **Purpose**: Drive iOS simulator operations (build, install, launch, logs)
- **Configuration**: iSH.xcodeproj, iSH scheme, iPhone 17 Pro simulator
- **Key Operations**: session_show_defaults, discover_projs, list_schemes, list_sims, build_run_sim, launch_app_logs_sim, erase_sims

### Boot Milestone Auditor
- **Purpose**: Extract ordered boot milestones from simulator logs
- **Milestones**: app_launched → boot_setup_started → first_elf_exec_entered → first_elf_exec_returned → second_execve_started → bin_login_elf_header_parsed → bin_login_program_headers_read → guest_loop_entered → login_ready → shell_ready
- **Output**: boot_milestones.json with milestone ordering and timestamps

### Crash Classifier
- **Purpose**: Normalize crash signatures for consistent classification
- **Algorithm**: sha256 of exception_type, signal, crashing_thread_backtrace_hash
- **Context**: Captures highest_completed and first_failing milestones
- **Output**: crash_signature.json, normalized_hash.txt, milestone_context.json

### Self-Healing Runner
- **Purpose**: Bounded retry/reset discipline for crash scenarios
- **Policy**: Max 1 retry, reset simulator on retry, same signature stops
- **Output**: reset_result.json, relaunch_result.json, retry_log.json

## Data Flow

```
Case Execution:
  1. Read case.yaml (discovery flow, artifacts, success criteria)
  2. Execute XcodeBuildMCP operations per case type
  3. Capture simulator logs
  4. Extract boot milestones (if applicable)
  5. Classify crashes (if applicable)
  6. Produce required artifacts
  7. Verify against expected.yaml
  8. Update status.yaml
```

## Case Types

### APPSIM (Harness Capability)
- APPSIM-001: XcodeBuildMCP discovery
- APPSIM-002: Simulator boot
- APPSIM-003: Build/install/launch smoke test
- APPSIM-004: Log harvest and milestone capture
- APPSIM-005: Crash signature normalization
- APPSIM-006: Bounded reset/relaunch

### APP (Runtime Entry)
- APP-001..005: Boot milestone progression to shell ready
- APP-003 is "smallest lawful app case" for crash reduction

### APP (Stability)
- APP-006..010: Relaunch stability and iOS lifecycle

## Phase Dependencies

```
02b (APPSIM-001..006) → 02c (APP-001..005) → 10 (APP-006..010)
```

Each phase gates the next. All gate cases in a phase must be REAL PASS before proceeding.

## Artifact Schema

### sim_launch.json
```json
{
  "case_id": "APPSIM-003",
  "launch_timestamp": "2026-03-28T12:00:00Z",
  "simulator_id": "E6186E89-8784-473B-A4E4-66E42693F14E",
  "bundle_id": "com.rudironsoni.ish",
  "success": true
}
```

### boot_milestones.json
```json
{
  "milestones": [
    {"name": "app_launched", "timestamp": "2026-03-28T12:00:01Z"},
    {"name": "boot_setup_started", "timestamp": "2026-03-28T12:00:02Z"}
  ],
  "highest_completed": "boot_setup_started",
  "first_failing": null
}
```

### crash_signature.json
```json
{
  "algorithm": "sha256",
  "fields": ["exception_type", "signal", "crashing_thread_backtrace_hash"],
  "hash": "abc123...",
  "raw_signature": { ... }
}
```

## Key Invariants

1. **Milestone Order**: Boot milestones must appear in defined order
2. **Crash Consistency**: Same crash scenario produces same normalized hash
3. **Retry Budget**: Max 1 retry per case, same signature stops
4. **Artifact Completeness**: All required artifacts must be produced for REAL PASS
