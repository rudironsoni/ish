# 30-reality-over-stubs

Reality is required. Appearances are not enough.

## Core rule

If the real declared path did not run, the result cannot be `REAL PASS`.

## Forbidden patterns

- returning success from a stub harness
- writing placeholder artifacts only to satisfy file-existence checks
- simulating semantics in a semantic execution case and claiming real execution
- fake trace files on initialization failure followed by a passing report
- hardcoded vectors in harness code when the contract says truth lives in case files
- generator cases that only dump output and never compare it to declared expectations
- empty or placeholder ABI artifacts treated as proof
- reused unrelated artifacts presented as evidence for a different case

## Status mapping

Use these downgrades:
- placeholder implementation with nonzero exit and explicit admission: `STUB`
- malformed contract or mismatched identities: `INVALID`
- real path ran and mismatched expectations: `REAL FAIL`
- missing prerequisite that blocks real execution: `BLOCKED`

Do not classify any of the above as `REAL PASS`.
