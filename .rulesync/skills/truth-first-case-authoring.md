---
name: truth_first_case_authoring
description: Author deterministic case contracts with truth chain established before any implementation work.
---

You are the truth-first case authoring skill. Apply this procedure when creating or updating case contracts.

## Purpose

Ensure every case has explicit normative truth established before implementation begins.

## Core Principle

Truth before code. Expected behavior must be written down before implementation changes.

## Prerequisites

- Case substrate exists (folder, identity, harness kind)
- Relevant truth agents are identified based on case class
- Case scope is clear and bounded

## Procedure

### 1. Establish Authority Chain
Before writing expectations:

**For decode cases:**
- Consult decoder truth for exact encoding and decoded fields
- Verify endianness and byte order
- Confirm canonical disassembly requirements

**For generator cases:**
- Consult generator truth for stable semantic expectations
- Identify unstable fields that must not be treated as golden
- Define acceptable gadget classes and counts

**For execution cases:**
- Consult ISA truth for register, memory, flag, and PC effects
- Define exception behavior if relevant
- Mark out-of-scope areas explicitly

**For ABI cases:**
- Consult ABI truth for stack layout, alignment, auxv requirements
- Define required argv/envp/auxv invariants
- Specify required artifact evidence

**For ELF cases:**
- Consult ELF truth for PT_LOAD expectations, entry requirements, loader invariants
- Define fixture requirements
- Specify runtime evidence needed

### 2. Write expected.yaml

Include explicit sections:
- **input**: Exact inputs (encodings, fixtures, initial state)
- **expectations**: Normative expected outputs with exact fields
- **success_criteria**: Precise conditions for REAL PASS
- **artifact_requirements**: Required artifacts and their schemas

Do not include:
- Vague prose-only truth
- Placeholder fields marked TBD
- Hardcoded harness vectors

### 3. Write authority.yaml

Include relevant sections:
- **semantic_authority**: Source of instruction or behavior semantics
- **decode_authority**: Tools used for encoding verification (e.g., llvm-mc)
- **generator_authority**: Stable vs unstable fields, acceptable outputs
- **abi_authority**: ABI specification references
- **elf_authority**: ELF specification references
- **toolchain_notes**: Exact commands and versions for verification
- **known_gaps**: Explicitly out-of-scope areas
- **non_authoritative_fields**: Fields that are observational, not normative

### 4. Validate Truth Chain

Check:
- Normative expectations are separated from commentary
- Stable fields are distinguished from unstable
- The authority chain is specific enough to write expected.yaml deterministically
- Out-of-scope areas are named, not implied

If the truth chain is incomplete, classify the case as INVALID and refuse to proceed with implementation.

### 5. Define Fixture Manifest (if applicable)

For fixture-based cases:
- Record fixture identity
- Record provenance (source, build recipe)
- Record checksum (SHA256)
- Record size
- Record expected format/shape
- Document regeneration steps

### 6. Verify Explicit Meson Identity

Confirm:
- Case ID matches folder name prefix
- Harness name is explicit
- Meson test name is explicit and matches case contract
- Artifact directory is deterministic

### 7. Refuse to Proceed When Invalid

Stop and report INVALID if:
- Normative fields are missing or TBD
- Authority chain is incomplete
- Fixture provenance is unspecified
- Case identity is inconsistent

## Forbidden Patterns

- Writing expectations to match current buggy output
- Leaving normative truth only in harness code
- Treating unstable implementation details as golden
- Proceeding with "we'll figure out the details later"

## Success Criteria

- Another agent can implement the case without guessing semantics
- The verifier can compare artifacts deterministically
- The authority chain is explicit and auditable
- The case is ready for bounded implementation
