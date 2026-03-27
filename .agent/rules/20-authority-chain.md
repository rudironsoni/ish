---
trigger: always_on
---
# 20-authority-chain

Normative truth must be declared explicitly in `authority.yaml`.

## Purpose

`authority.yaml` explains what is normative, where the expectation comes from, what is stable, and what is intentionally out of scope.

## Required sections

Use the sections that apply:
- `semantic_authority`
- `decode_authority`
- `generator_authority`
- `abi_authority`
- `elf_authority`
- `toolchain_notes`
- `known_gaps`
- `non_authoritative_fields`

## Requirements

- Normative expectations must be separated from commentary.
- Stable fields must be distinguished from unstable fields.
- The authority chain must be specific enough that `expected.yaml` can be written deterministically.
- Out-of-scope areas must be named, not implied.

## Forbidden patterns

- "architecturally correct" with no field-level expectations
- prose-only truth that cannot drive verification
- hiding unstable implementation details inside supposed goldens
