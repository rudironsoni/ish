# slop-scan

Scan the active case or recent changes for slop, overclaiming, fake success, and process drift.

## Purpose

Use this command before claiming completion and when a suspicious pattern appears.

## Required checks

Look for:
- stub comments with passing exits
- fake artifact generation
- success-on-fallback behavior
- hardcoded case expectations living in harnesses
- generator cases that never compare against goldens
- Meson identity mismatch
- dynamic discovery or hidden dispatch behavior
- case-contract drift
- docs/process drift that should be encoded into `.ai/`

## Outputs

- slop report
- remediation list
- status downgrade recommendation if needed

## Refuse if

- the active case is unknown
