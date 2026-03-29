# User Testing: iSH Runtime Entry

## Validation Surface

**Primary Surface:** iOS Simulator via XcodeBuildMCP

**Tools:**
- `XcodeBuildMCP___build_run_sim` - Build and run app on simulator
- `XcodeBuildMCP___launch_app_logs_sim` - Launch app with log capture
- `XcodeBuildMCP___screenshot` - Capture screenshots
- `XcodeBuildMCP___get_sim_app_path` - Get built app path

**Test Flow:**
1. Build iSH app for simulator
2. Boot simulator
3. Install and launch app
4. Capture logs
5. Extract milestones from logs
6. Generate JSON artifacts
7. Validate against expected.yaml (for cases with expectations)

## Validation Concurrency

**Max Concurrent Validators:** 1

**Rationale:**
- iOS Simulator is resource-intensive
- Only one simulator can be active at a time
- Each app launch requires exclusive simulator access
- Running multiple validators would serialize anyway

**Resource Cost Classification:**
- High: Simulator boot takes ~30-60 seconds
- High: App build takes ~2-5 minutes
- Medium: Log capture and analysis
- Low: JSON artifact generation

## Testing Notes

**App Launch Sequence:**
1. App launches
2. Boot setup started
3. First ELF exec entered
4. First ELF exec returned
5. Second execve started (/bin/login)
6. Guest loop entered
7. Login ready (or crash)

**Crash Signature (APP-003):**
- Captures highest_completed_milestone
- Captures first_failing_milestone
- Normalizes crash signature hash
- Used to track crash reduction progress

**Known Issues:**
- NULL current->mem crash occurs during early boot
- Crash prevents reaching login_ready milestone
- APP-003 designed to capture this specific crash
