# Simulator Constraint Rule

## HARD CONSTRAINT

**ONLY iPhone 17 Pro is permitted for APPSIM-004 testing.**

## Approved Simulator

- **Name:** iPhone 17 Pro
- **UUID:** E6186E89-8784-473B-A4E4-66E42693F14E
- **Status:** Must be Booted

## Forbidden Simulators

- **iPhone 17** (971A2EC4-3492-4F21-8D79-96E6863127B4) - NEVER USE
- **iPhone 17 Pro Max** - NEVER USE
- **iPhone 17e** - NEVER USE
- **iPhone Air** - NEVER USE
- Any other iPhone simulator - NEVER USE

## Enforcement

1. Before any build/run/test operation:
   - Verify session defaults point to iPhone 17 Pro
   - Check that iPhone 17 is NOT running
   - If iPhone 17 is running, shut it down immediately

2. XcodeBuildMCP operations:
   - Must use simulatorId: E6186E89-8784-473B-A4E4-66E42693F14E
   - Must NOT use any other simulator UUID

3. Verification command:
   ```bash
   xcrun simctl list devices | grep -E "iPhone 17"
   ```
   Expected output must show:
   - iPhone 17 Pro (E6186E89...) (Booted)
   - iPhone 17 (971A2EC4...) (Shutdown)

## Consequences of Violation

Using iPhone 17 instead of iPhone 17 Pro:
- Contaminates test results with wrong device context
- Wastes time on wrong simulator
- Violates APPSIM-004 test protocol
- Must be corrected immediately

## Configuration

Session defaults must be persisted to:
- `.xcodebuildmcp/config.yaml`

With values:
```yaml
sessionDefaults:
  simulatorName: iPhone 17 Pro
  simulatorId: E6186E89-8784-473B-A4E4-66E42693F14E
```
