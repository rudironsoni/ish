---
name: Test Migration - User Directions
---

# Test Migration Policy

## Decision Record

**Status:** VERIFIED
**Date:** 2026-03-28

### Decisions Made

1. **EXEC-REAL-001 is now Phase 03 gate case**
   - Added as first gate case in `execution-order.yaml`
   - Bootstrap sequence updated: EXEC-REAL-001 replaces EXEC-001

2. **Real TCTI execution test wrapped in semantic_micro_harness**
   - `test_tcti_exec_str_postindex.c` was source reference
   - Adapted to harness pattern via case.yaml

3. **Old tests deleted after migration**
   - `tests/Unit/` - XCTest files (all XCTSkip anyway)
   - `tests/e2e/` - bash-based E2E tests
   - `tests/aarch64/test_tcti_exec_str_postindex.c` - standalone C test

## Migrated Coverage

### Tests Moved to Case System

| Source | Destination | Status |
|--------|-------------|--------|
| tests/aarch64/test_tcti_exec_str_postindex.c | EXEC-REAL-001 | REGISTERED |

### Tests Deleted (Covered Elsewhere or Irrelevant)

| Source | Reason |
|--------|--------|
| tests/Unit/DecoderTests.m | Covered by DEC-001..DEC-011 |
| tests/Unit/GeneratorTests.m | Covered by GEN-001..GEN-008 |
| tests/Unit/GadgetTests.m | CPU structure only, no semantic value |
| tests/Unit/EmulatorTests.m | All XCTSkip |
| tests/Unit/IntegrationTests.m | Decode patterns covered |
| tests/e2e/* | Replaced by MUSL/GLIBC/DISTRO cases |

## Remaining Directory Structure

```
tests/
├── aarch64/          # Standalone C tests (kept)
├── cases/            # Case system (EXEC-REAL-001 ADDED)
└── [Unit/ deleted]   # XCTests removed
├── [e2e/ deleted]    # bash E2E removed
```

## Verification

To verify:
1. Check `tests/cases/03-semantic-exec/EXEC-REAL-001-str-execution/case.yaml` exists
2. Check `tests/cases/meson.build` has case:EXEC-REAL-001 entry
3. Check `tests/cases/execution-order.yaml` has EXEC-REAL-001 before EXEC-001
4. Check `tests/Unit/` does not exist
5. Check `tests/e2e/` does not exist
