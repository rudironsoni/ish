# Environment: iSH Mission

## Build System

**Meson** - Primary build system
- Setup: `meson setup builddir`
- Build: `meson compile -C builddir`
- Test: `meson test -C builddir`

## iOS Development

**Xcode Required**
- Xcode command line tools
- iOS Simulator
- xcrun, simctl tools

**XcodeBuildMCP Tools**
Available via MCP - used for simulator operations

## Test Case Structure

**Location:** `tests/cases/02c-ios-app-runtime-entry/`

**Per Case:**
- Directory: `APP-XXX-case-name/`
- case.yaml - Case definition
- expected.yaml - Expected results (optional)
- report.json - Generated report (output)
- *.json - Generated artifacts (output)

## Dependencies

**External:**
- 02b-harness-capability must be REAL PASS
- XcodeBuildMCP for simulator testing

**Internal:**
- ios_app_harness.c - Test harness
- trace.h - Diagnostic logging infrastructure

## Environment Variables

None specific to this mission - all configuration via Meson and Xcode.
