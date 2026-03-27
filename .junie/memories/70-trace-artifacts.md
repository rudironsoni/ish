# 70-trace-artifacts

Trace artifacts are evidence, not decoration.

## Required trace-contract fields

If a case depends on trace evidence, the contract must define:
- required trace artifacts
- artifact paths
- artifact schema or decoding expectations
- required event classes
- failure behavior when trace initialization fails

## Requirements

- Fake or placeholder trace files cannot satisfy a trace requirement.
- A trace artifact is only useful if it can be interpreted in the context of the case.
- Required trace fields must appear in `case.yaml` and `expected.yaml` as appropriate.

## Forbidden patterns

- passing after writing a stub trace file because tracing failed to initialize
- requiring trace evidence but not defining what events or fields matter
- presence-only trace checks with no semantic use
