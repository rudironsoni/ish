# User Testing Guide: iOS App Testing Harness

## Testing Surface: XcodeBuildMCP iOS Simulator

The primary testing surface is the iOS Simulator controlled via XcodeBuildMCP tools. Tests validate the iSH app running in the simulator through build, install, launch, and log capture operations.

## XcodeBuildMCP Configuration

- **project_path**: `/Users/rudironsoni/src/github/rudironsoni/ish/iSH.xcodeproj`
- **scheme**: `iSH`
- **configuration**: `Debug`
- **simulator_name**: `iPhone 17 Pro`
- **simulator_id**: `E6186E89-8784-473B-A4E4-66E42693F14E`
- **bundle_id**: `com.rudironsoni.ish`

## Validation Concurrency

**Max concurrent validators: 1**

Rationale: iOS Simulator instances are heavyweight (~500MB RAM each). Xcode builds during test execution consume ~2GB RAM. Concurrent simulator operations can interfere with each other and cause resource contention.

## Flow Validator Guidance: XcodeBuildMCP iOS Simulator

### Isolation Rules

1. **Single Simulator Instance**: Only one simulator operation at a time. All assertions in this surface must be tested serially.
2. **No Shared State**: Each test case should start from a clean simulator state when possible.
3. **Reset Between Major Tests**: Use `erase_sims` followed by `boot_sim` when testing reset/relaunch scenarios.
4. **Artifact Locations**: Write all evidence to `.factory/validation/02b-harness-capability/user-testing/flows/<group-id>/`

### Testing Tools

**Primary Tool**: XcodeBuildMCP MCP tools
- `session_show_defaults` - Verify configuration
- `discover_projs` - Find Xcode projects
- `list_schemes` - List build schemes
- `list_sims` - List simulator targets
- `boot_sim` - Boot simulator
- `build_run_sim` - Build, install, and launch app
- `launch_app_logs_sim` - Launch with log capture
- `erase_sims` - Reset simulator
- `get_sim_app_path` - Get built app path
- `stop_app_sim` - Stop running app

**Secondary Tool**: File system validation
- Check artifact files exist and contain expected content
- Validate JSON structure
- Verify log content

### Assertion Testing Patterns

#### VAL-APPSIM-001 through VAL-APPSIM-005 (Discovery)

Test pattern:
1. Call `session_show_defaults` → verify valid config
2. Call `discover_projs` → verify iSH.xcodeproj found
3. Call `list_schemes` → verify "iSH" scheme present
4. Call `list_sims` → verify simulators available
5. Verify bundle_id matches "com.rudironsoni.ish"

#### VAL-APPSIM-006 through VAL-APPSIM-007 (Simulator Boot)

Test pattern:
1. Call `boot_sim` → verify boot success
2. Call `list_sims` with enabled flag → verify runtime available
3. Capture boot status and runtime version

#### VAL-APPSIM-008 through VAL-APPSIM-010 (Build/Install/Launch)

Test pattern:
1. Call `build_run_sim` → get build output
2. Verify build succeeded (check for .app bundle)
3. Verify install succeeded (bundle ID present)
4. Verify launch succeeded and app is alive

#### VAL-APPSIM-011 through VAL-APPSIM-012 (Log Harvest)

Test pattern:
1. Call `launch_app_logs_sim` → capture logs during launch
2. Extract boot milestones from logs
3. Verify milestone ordering (app_launched first)

#### VAL-APPSIM-013 through VAL-APPSIM-015 (Crash Signature)

Test pattern:
1. Launch app and capture logs
2. If crash occurs, extract crash signature
3. Normalize hash (consistent across runs)
4. Capture milestone context (highest_completed, first_failing)

#### VAL-APPSIM-016 through VAL-APPSIM-018 (Reset/Relaunch)

Test pattern:
1. Call `erase_sims` → verify reset success
2. Call `boot_sim` → re-boot simulator
3. Call `launch_app_logs_sim` → relaunch app
4. Verify retry logging (max 1 retry)

### Artifact Validation

For each assertion group, validate these artifacts:

**Discovery artifacts**:
- `xcodebuildmcp_capability.json` - Config from session_show_defaults
- `app_target_identity.json` - Bundle ID, scheme, project path
- `simulator_targets.json` - Available simulators

**Boot artifacts**:
- `boot_status.json` - Boot result status
- `device_runtime.json` - iOS runtime version
- `simulator_target.json` - Selected device

**Build/Launch artifacts**:
- `build_result.json` - Build success status, app path
- `install_result.json` - Install success, bundle ID
- `launch_result.json` - Launch success status
- `alive_check.json` - App alive verification
- `sim_launch.json` - Launch metadata

**Log artifacts**:
- `simulator_log_tail.txt` - Captured logs
- `boot_milestones.json` - Extracted milestones

**Crash artifacts**:
- `crash_signature.json` - Signature algorithm, fields, hash
- `normalized_hash.txt` - Consistent hash value
- `milestone_context.json` - highest_completed, first_failing

**Reset artifacts**:
- `reset_result.json` - Reset success status
- `relaunch_result.json` - Relaunch success status
- `retry_log.json` - Retry count and attempts

### Required Evidence Format

Each flow validator must produce:

```json
{
  "groupId": "<group-name>",
  "assertionsTested": ["VAL-APPSIM-001", ...],
  "status": "pass|fail|blocked",
  "results": [
    {
      "assertionId": "VAL-APPSIM-001",
      "status": "pass|fail|blocked",
      "evidence": "path/to/evidence.json",
      "details": "..."
    }
  ],
  "frictions": [],
  "blockers": [],
  "toolsUsed": ["XcodeBuildMCP"]
}
```

### Common Issues and Solutions

1. **Simulator already in use**: Stop any running simulator app or use `stop_app_sim`
2. **Build timeout**: Increase timeout or check for compilation errors
3. **Bundle ID mismatch**: 
   - Validation contract expects "com.rudironsoni.ish" (per services.yaml)
   - Actual bundle ID in Xcode project is "app.ish.iSH"
   - Use actual bundle ID from session_show_defaults for testing
   - This discrepancy should be resolved by updating validation contract or Xcode config
4. **Log capture empty**: Ensure app actually launched and produced output
5. **Boot failures**: Try `erase_sims` before `boot_sim` for clean state

### Timing Requirements

- Build operations: up to 5 minutes
- Launch operations: up to 1 minute
- Log capture: up to 30 seconds
- Boot operations: up to 2 minutes
