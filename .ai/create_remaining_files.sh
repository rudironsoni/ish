#!/bin/bash
# Script to create all remaining .ai/ control plane files (rules, commands, hooks)

set -e

mkdir -p .ai/rules .ai/commands .ai/hooks

# Rules
cat > .ai/rules/00-non-negotiables.md << 'EOF'
# 00-non-negotiables

These are repository-wide non-negotiable rules.

## Mandatory rules

1. No stub may count as pass.
2. No dynamic case discovery.
3. No hidden runtime dispatcher.
4. No direct edits to generated outputs.
5. No phase skipping.
6. No success-on-fallback behavior.
7. No case without explicit Meson wiring.
8. No vague completion claim without verifier and anti-slop review.
9. No later-phase progression while earlier gate cases are not `REAL PASS`.
10. No broad runtime debugging when a smaller failing case can be authored or repaired first.

## Required status vocabulary

Use only:
- `REAL PASS`
- `REAL FAIL`
- `STUB`
- `BLOCKED`
- `INVALID`

Do not invent softer synonyms.

## Repository posture

- Prefer smaller units.
- Prefer explicit contracts.
- Prefer deterministic fixtures.
- Prefer repo-local truth.
- Prefer real execution paths over simulated proof.
EOF

cat > .ai/rules/10-case-contract.md << 'EOF'
# 10-case-contract

A valid case is a deterministic contract.

## Required files

Every non-trivial case MUST include:
- `case.yaml`
- `expected.yaml`
- `authority.yaml`

Every fixture-based case MUST also include:
- `fixtures/manifest.yaml`

Optional files:
- `README.md`
- `notes.md`

## Required case fields

At minimum, the case contract MUST define:
- exact case ID
- exact phase
- exact harness name
- exact Meson test identity
- prerequisites
- success criteria
- required artifacts
- allowed patch scope
- trace defaults when tracing matters
- informational-only flag if applicable

## Consistency requirements

The following MUST agree:
- folder prefix
- `case.yaml:id`
- harness identity
- Meson test name
- deterministic artifact directory naming

If these drift, the case is `INVALID`.

## Forbidden patterns

- normative expectations only inside harness code
- placeholder truth in place of explicit fields
- artifact presence checks with no schema or semantic comparison
- implicit fixtures with no manifest or provenance
EOF

cat > .ai/rules/20-authority-chain.md << 'EOF'
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
EOF

cat > .ai/rules/30-reality-over-stubs.md << 'EOF'
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
EOF

cat > .ai/rules/40-phase-gating.md << 'EOF'
# 40-phase-gating

Bootstrap phases are ordered and gated.

## Source of truth

Phase order and gate cases live in:
- `tests/cases/execution-order.yaml`

## Rule

A later phase may proceed only when the earlier gate conditions are satisfied.

Only `REAL PASS` satisfies a gate case.

The following do not unlock later phases:
- `REAL FAIL`
- `STUB`
- `BLOCKED`
- `INVALID`

## Required behavior

- If a later-phase symptom appears first, reduce the problem to the earliest unsatisfied prerequisite case.
- If a prerequisite case does not exist, create it first.
- Do not use broad runtime smoke as justification for skipping missing earlier proof.

## Goal

Keep the bootstrap sequence honest and compositional.
EOF

cat > .ai/rules/50-meson-only.md << 'EOF'
# 50-meson-only

Case entrypoints must be explicit and discoverable from Meson.

## Required behavior

- Register each case explicitly in Meson.
- Register each harness target explicitly in Meson.
- Keep case identity aligned between case files and Meson entries.

## Forbidden patterns

- dynamic case discovery
- hidden runtime dispatchers
- Python runtime dispatch used to replace explicit case registration
- shell-script public workflows used in place of explicit Meson test entries
- untracked case aliases

## Outcome

Another agent must be able to locate the exact case entrypoint from Meson files without guessing.
EOF

cat > .ai/rules/60-deterministic-fixtures.md << 'EOF'
# 60-deterministic-fixtures

Fixtures must be deterministic and auditable.

## Required fixture manifest fields

For every non-trivial fixture, record:
- identity
- provenance
- checksum
- size
- expected format or shape
- regeneration steps or acquisition steps if applicable

## Requirements

- Fixture meaning must be documented in repo-local files.
- Checksums must be stable and explicit.
- Fixture metadata must match the active case contract.

## Forbidden patterns

- floating binary fixtures with no provenance
- fixture placeholders treated as final inputs
- case expectations that implicitly depend on undocumented fixture structure
EOF

cat > .ai/rules/70-trace-artifacts.md << 'EOF'
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
EOF

cat > .ai/rules/80-patch-budget.md << 'EOF'
# 80-patch-budget

Every repair loop must be bounded.

## Before implementation or repair, declare

- active case ID
- current case status
- smallest failing unit
- allowed patch scope
- maximum retry count
- stop conditions

## Required behavior

- Stay inside the declared patch scope unless the case contract itself is invalid and must be repaired first.
- Re-run the explicit Meson target after each bounded repair loop.
- Re-run verifier and anti-slop before claiming progress.

## Forbidden patterns

- scope creep across multiple active cases
- widening patch scope without explicit justification
- continuing indefinitely without retry or stop discipline
EOF

cat > .ai/rules/90-doc-legibility.md << 'EOF'
# 90-doc-legibility

Repository-local instructions are part of correctness.

## Principles

- The repo is the system of record for future agents.
- If a rule matters more than once, it should be encoded in `.ai/`.
- `AGENTS.md` should be a navigable map, not an unstructured encyclopedia.

## Required behavior

- Update `.ai/` when a repeated rule, pattern, or failure mode becomes important.
- Remove or fix stale instructions that conflict with the real repository.
- Keep cross-links between process files coherent.

## Forbidden patterns

- critical workflow knowledge living only in chat
- stale instructions left in place after process changes
- giant instruction blobs with no structure or cross-linking
EOF

cat > .ai/rules/95-review-policy.md << 'EOF'
# 95-review-policy

Implementation is not self-certifying.

## Required reviews before a completion claim

Before any "done", "fixed", or `REAL PASS` claim:
- verifier must review
- anti-slop must review
- review-skeptic must review
- phase-gate must approve forward movement if phase progression is implicated

## Rule

The same implementation agent must not be the only authority approving its own work.

## Forbidden patterns

- implementation-only success claims
- bypassing skeptical review because the code "looks right"
- phase progression based on unverified implementation narrative
EOF

echo "All rules created successfully"

# Commands
cat > .ai/commands/case-new.md << 'EOF'
# case-new

Create a deterministic new case before implementation begins.

## Purpose

Use this command when a task is not already covered by an existing valid case.

## Required sequence

1. Run `.ai/hooks/pre-case-create.md`
2. Determine:
   - case ID
   - phase
   - slug
   - harness kind
   - prerequisites
3. Invoke `case-substrate`
4. Invoke the relevant truth agents
5. Invoke `fixture-author` if fixtures are needed
6. Invoke `case-author`
7. Invoke `meson-wire`
8. Run `.ai/hooks/post-case-create.md`
9. Run `.ai/commands/case-audit.md`

## Required outputs

- deterministic case folder
- `case.yaml`
- `expected.yaml`
- `authority.yaml`
- `fixtures/manifest.yaml` if applicable
- explicit Meson registration

## Refuse if

- the requested work belongs to an existing valid case
- the phase is ambiguous and unresolved
- the resulting case would violate `.ai/rules/50-meson-only.md`
EOF

cat > .ai/commands/case-run.md << 'EOF'
# case-run

Run exactly one active case through its explicit Meson entrypoint and collect deterministic evidence.

## Purpose

Use this command to run the active case after the contract is valid.

## Required sequence

1. Run `.ai/hooks/pre-run.md`
2. Confirm:
   - active case ID
   - exact Meson test identity
   - artifact directory
   - verifier expectations
3. Run the explicit Meson case target
4. Collect emitted artifacts
5. Invoke `verifier`
6. Invoke `anti-slop`
7. If phase movement is implicated, invoke `phase-gate`

## Required outputs

- explicit run result
- artifact set
- exact case status
- mismatch report if not `REAL PASS`

## Refuse if

- the case contract is `INVALID`
- Meson identity is inconsistent
- the harness still contains known fake-success fallback behavior
EOF

cat > .ai/commands/case-repair.md << 'EOF'
# case-repair

Repair one failing active case within a declared patch budget.

## Purpose

Use this command when the active case is not `REAL PASS` and the next action is a bounded repair loop.

## Required sequence

1. Run `.ai/hooks/on-failure.md`
2. Classify the current status:
   - `REAL FAIL`
   - `STUB`
   - `BLOCKED`
   - `INVALID`
3. Reduce to the smallest failing unit
4. Declare:
   - active case
   - allowed patch scope
   - retry budget
   - stop conditions
5. Invoke the necessary specialist agent
6. Re-run `.ai/commands/case-run.md`
7. Re-run verifier and anti-slop

## Required outputs

- bounded repair plan
- updated evidence
- updated exact status

## Refuse if

- multiple active cases are being mixed
- the case contract is invalid and needs contract repair before implementation
- the repair would illegally widen into a different phase without gate approval
EOF

cat > .ai/commands/case-audit.md << 'EOF'
# case-audit

Audit a case for deterministic contract integrity.

## Purpose

Use this command to validate that a case is structurally ready for implementation or verification.

## Audit checklist

Check all of the following:
- folder name and case ID match
- `case.yaml`, `expected.yaml`, and `authority.yaml` exist
- `fixtures/manifest.yaml` exists when needed
- harness name is explicit
- Meson test name is explicit
- Meson test name matches the case contract
- artifact directory naming is deterministic
- prerequisites are explicit
- allowed patch scope is explicit
- trace defaults are explicit for trace-dependent cases
- no normative truth lives only inside harness code

## Outputs

- PASS or FAIL audit result
- exact inconsistency list
- `INVALID` recommendation if the contract is malformed

## Refuse if

- the case path does not resolve
- the case files are so incomplete that the audit cannot begin
EOF

cat > .ai/commands/phase-audit.md << 'EOF'
# phase-audit

Audit whether the current repository state permits forward movement in phase order.

## Purpose

Use this command before moving into a later phase or when a later-phase symptom appears.

## Required sequence

1. Read `tests/cases/execution-order.yaml`
2. Collect current statuses for prerequisite and gate cases
3. Identify the earliest unsatisfied gate
4. Invoke `phase-gate`

## Outputs

- allowed or blocked decision
- earliest unsatisfied case
- exact reason progression is denied if blocked

## Refuse if

- current case statuses are not known
- the phase inventory is malformed
EOF

cat > .ai/commands/truth-sync.md << 'EOF'
# truth-sync

Refresh or reconcile truth-chain files when case expectations are missing, stale, or inconsistent.

## Purpose

Use this command when `authority.yaml` or `expected.yaml` need to be aligned with current declared case scope.

## Required sequence

1. Identify the active case
2. Identify which truth domains apply:
   - ISA
   - decode
   - generator
   - ABI
   - ELF
3. Invoke the relevant truth agents
4. Update `authority.yaml`
5. Update `expected.yaml` through `case-author`
6. Re-run `.ai/commands/case-audit.md`

## Outputs

- synchronized truth chain
- reduced ambiguity in normative expectations

## Refuse if

- the case scope is undefined
- the requested truth update would hide unstable implementation details as normative without explicit declaration
EOF

cat > .ai/commands/slop-scan.md << 'EOF'
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
EOF

cat > .ai/commands/doc-garden.md << 'EOF'
# doc-garden

Maintain repo-local instruction legibility.

## Purpose

Use this command when a repeated rule, process change, or stale instruction needs to be reflected in the repository.

## Required sequence

1. Identify the repeated rule or stale instruction
2. Decide whether it belongs in:
   - `AGENTS.md`
   - `.ai/agents/*`
   - `.ai/rules/*`
   - `.ai/commands/*`
   - `.ai/hooks/*`
3. Update the smallest correct file set
4. Preserve cross-links and navigability
5. Avoid turning `AGENTS.md` into a giant unstructured manual

## Outputs

- updated repo-local instructions
- clearer future-agent guidance

## Refuse if

- the requested doc change contradicts current enforced repository policy and no policy decision exists
EOF

echo "All commands created successfully"

# Hooks
cat > .ai/hooks/pre-task.md << 'EOF'
# pre-task

Run this before any non-trivial work.

## Checklist

1. Identify the user task.
2. Identify whether an existing case already covers it.
3. Identify the active phase.
4. Identify the earliest relevant unsatisfied gate case.
5. Decide whether work should:
   - use an existing case
   - create a new case
   - repair an existing failing case
6. Confirm one active implementation case only.
7. Confirm required `.ai/rules/*` files have been read.

## Output

A short task header with:
- active case
- phase
- current status
- smallest failing unit
- next delegated agent
EOF

cat > .ai/hooks/pre-case-create.md << 'EOF'
# pre-case-create

Run this before creating a new case.

## Checklist

1. Confirm no valid existing case already covers the task.
2. Choose exact phase.
3. Choose exact case ID and slug.
4. Choose harness kind.
5. Determine which truth agents are required.
6. Determine whether fixtures are required.
7. Determine whether the new case is a gate case or subordinate case.

## Output

A deterministic case-creation brief.
EOF

cat > .ai/hooks/post-case-create.md << 'EOF'
# post-case-create

Run this immediately after creating a new case.

## Checklist

1. Confirm folder name matches case ID.
2. Confirm `case.yaml`, `expected.yaml`, and `authority.yaml` exist.
3. Confirm `fixtures/manifest.yaml` exists when needed.
4. Confirm harness identity is explicit.
5. Confirm Meson test identity is explicit.
6. Confirm Meson test identity matches the case contract.
7. Confirm no placeholder success claims exist.
8. Run `.ai/commands/case-audit.md`.

## Output

A structural validity report for the new case.
EOF

cat > .ai/hooks/pre-implementation.md << 'EOF'
# pre-implementation

Run this before changing implementation code for a case.

## Checklist

1. Confirm active case ID.
2. Confirm current exact status.
3. Confirm case contract is valid.
4. Confirm prerequisites and phase gates are satisfied.
5. Confirm allowed patch scope.
6. Confirm retry budget and stop conditions.
7. Confirm the real path to be exercised is known.

## Output

A bounded implementation brief.
EOF

cat > .ai/hooks/pre-run.md << 'EOF'
# pre-run

Run this before executing a case.

## Checklist

1. Confirm case identity and Meson identity match.
2. Confirm expected artifacts are known.
3. Confirm verifier knows what fields are normative.
4. Confirm fake-success fallback behavior is not being relied on.
5. Confirm artifact directory is deterministic.
6. Confirm trace expectations are explicit if tracing is required.

## Output

A run-readiness check.
EOF

cat > .ai/hooks/on-failure.md << 'EOF'
# on-failure

Run this when a case run does not achieve `REAL PASS`.

## Checklist

1. Classify exactly:
   - `REAL FAIL`
   - `STUB`
   - `BLOCKED`
   - `INVALID`
2. Identify the smallest failing unit.
3. Determine whether the problem is:
   - truth-chain issue
   - case-contract issue
   - Meson wiring issue
   - harness implementation issue
   - product-code issue
4. Decide whether a bounded repair loop is allowed.
5. Do not widen scope casually.
6. Do not pivot to a different case to avoid the failure.

## Output

A failure classification and next action.
EOF

cat > .ai/hooks/pre-claim.md << 'EOF'
# pre-claim

Run this before any claim of completion or pass.

## Checklist

1. Verifier has reviewed the artifacts.
2. Anti-slop has reviewed the implementation and claims.
3. Review-skeptic has attempted to disprove the claim.
4. Phase-gate has approved progression if relevant.
5. Exact status vocabulary is used.
6. No overclaiming language remains.

## Output

A claim-readiness decision.
EOF

cat > .ai/hooks/pre-merge.md << 'EOF'
# pre-merge

Run this before merge or final completion reporting.

## Checklist

1. Case status is exact and verified.
2. No temporary fake-success scaffolding remains.
3. No stale status text remains in case files or harness comments.
4. `.ai/` docs are updated if process knowledge changed.
5. Meson wiring matches case contracts.
6. Later phases are not being unlocked illegally.

## Output

A merge-readiness decision.
EOF

echo "All hooks created successfully"
echo ""
echo "Summary:"
echo "- 17 agent files"
echo "- 11 rule files"
echo "- 8 command files"
echo "- 8 hook files"
echo "- 1 root AGENTS.md"
echo "- Total: 45 files"
