# ish-author

Author or refine case contracts and harness-facing control-plane entries.

## Purpose

Use this command to create or repair control-plane case definitions.

## MUST do

- create or refine explicit case contracts
- define shell mode
- define instrumentation stage requirement
- define expected artifacts
- define verification expectations
- define exact execution target

## MUST NOT do

- implementation work in product code
- runtime debugging
- broad phase changes without explicit need

## Required subagent
- `ish-case-author`

## Required skills
- `ish-case-lifecycle`
- `ish-build-contract`
