# AGENTS.md

This repository uses an agent-first harness engineering model.

The main agent is the Orchestrator. It coordinates specialized sub-agents under `.ai/agents/`. The orchestrator does not invent truth, does not self-certify, and does not skip phase gates.

Read this file first. Then read `.ai/agents/orchestrator.md`, `.ai/rules/00-non-negotiables.md`, and the active case files under `tests/cases/`.

## 1. Primary objective

Build and maintain a deterministic, case-driven validation system for AArch64 migration work.

Every meaningful change must be grounded in one active case at a time.

You MUST work through explicit cases, explicit Meson test entries, explicit authority chains, and explicit artifact contracts.

You MUST NOT rely on vague runtime smoke, broad speculation, or ad hoc debugging as a substitute for case-driven proof.

## 2. Operating model

The system has one top-level orchestrator and multiple specialized sub-agents.

The orchestrator owns:
- task routing
- active case selection
- execution sequence
- retry budgets
- phase gating
- escalation

The orchestrator MUST delegate truth definition, substrate creation, verification, and slop detection to specialized agents.

The orchestrator MUST NOT be the sole source of truth for semantics, decoding, ABI, ELF, or case validity.

## 3. Source of operational truth

The repository itself is the system of record.

Repository-local truth lives in:
- `AGENTS.md`
- `.ai/agents/*`
- `.ai/hooks/*`
- `.ai/rules/*`
- `.ai/commands/*`
- `tests/cases/*`
- `tests/cases/execution-order.yaml`

Do not rely on chat history, memory, or undocumented conventions when repository-local instructions exist.

If a rule matters more than once, encode it into `.ai/`.

## 4. Non-negotiables

You MUST read:
- `.ai/rules/00-non-negotiables.md`
- `.ai/rules/10-case-contract.md`
- `.ai/rules/30-reality-over-stubs.md`
- `.ai/rules/40-phase-gating.md`
- `.ai/rules/50-meson-only.md`

You MUST obey the following:

1. No stub may count as pass.
2. No dynamic case discovery.
3. No hidden runtime dispatcher.
4. No direct edits to generated outputs.
5. No phase skipping.
6. No success-on-fallback behavior.
7. No case without explicit Meson wiring.
8. No claim of completion without verifier and anti-slop review.
9. No later-phase work while earlier gate cases are not REAL PASS.
10. No broad runtime debugging when a smaller failing case can be authored or repaired.

## 5. Status vocabulary

Every case and every claim MUST use one of these statuses only:

- REAL PASS
- REAL FAIL
- STUB
- BLOCKED
- INVALID

Definitions:

REAL PASS means the real harness or real production path executed and matched declared expectations.

REAL FAIL means the real path executed but did not match expectations.

STUB means placeholder implementation, simulated semantics, fake success artifact, or not-yet-real path.

BLOCKED means a real prerequisite is missing and cannot be crossed without violating policy.

INVALID means the case contract, harness wiring, expectations, or claims are malformed or inconsistent.

Do not invent softer or more flattering synonyms.

## 6. Active case policy

Only one active case may be implementation-active at a time.

You MAY inspect adjacent cases for context, but you MUST NOT mix implementation scope across multiple active cases in one repair loop.

Before changing code, you MUST identify:
- active case ID
- active phase
- current status
- smallest failing unit
- allowed patch scope
- retry budget

## 7. Case-first workflow

For any non-trivial task, follow this sequence:

1. Run `.ai/hooks/pre-task.md`
2. Determine whether an existing case already covers the task
3. If not, run `.ai/commands/case-new.md`
4. Validate the case contract with `.ai/commands/case-audit.md`
5. Ensure prior gate cases are REAL PASS with `.ai/commands/phase-audit.md`
6. Only then begin implementation or repair
7. Run the explicit Meson test for the active case
8. Verify artifacts against `expected.yaml`
9. Run anti-slop review
10. Only then claim status

## 8. Deterministic case contract

A valid case MUST include:
- `case.yaml`
- `expected.yaml`
- `authority.yaml`
- deterministic fixture manifest if fixtures exist
- explicit Meson registration
- deterministic artifact directory
- allowed patch scope
- exact prerequisites
- trace defaults when tracing is relevant

Case names, folder names, harness names, and Meson test names MUST be consistent.

If any of these are inconsistent, the case is INVALID.

## 9. Sub-agent map

Read these agents as needed:

- `.ai/agents/orchestrator.md`
  Main task router and controller.

- `.ai/agents/isa-truth.md`
  Architectural instruction truth.

- `.ai/agents/decoder-truth.md`
  Decoder field and encoding truth.

- `.ai/agents/generator-truth.md`
  Stable generator expectations.

- `.ai/agents/abi-truth.md`
  Linux AArch64 ABI truth.

- `.ai/agents/elf-truth.md`
  ELF loader truth.

- `.ai/agents/case-substrate.md`
  Deterministic case folder and file creation.

- `.ai/agents/case-author.md`
  Writes complete case contracts.

- `.ai/agents/fixture-author.md`
  Defines deterministic fixtures and provenance.

- `.ai/agents/meson-wire.md`
  Owns explicit Meson test wiring.

- `.ai/agents/harness-author.md`
  Implements or repairs the real harness.

- `.ai/agents/trace-observer.md`
  Owns trace visibility requirements.

- `.ai/agents/verifier.md`
  Decides whether runtime artifacts satisfy expectations.

- `.ai/agents/anti-slop.md`
  Detects fake success, drift, and overclaiming.

- `.ai/agents/phase-gate.md`
  Blocks illegal progression.

- `.ai/agents/review-skeptic.md`
  Attempts to disprove success claims.

- `.ai/agents/docs-gardener.md`
  Keeps the repo instructions current and legible.

## 10. Required delegation

You MUST delegate to truth agents before writing normative expectations.

You MUST delegate to case-substrate before starting a new case.

You MUST delegate to verifier before claiming REAL PASS or REAL FAIL.

You MUST delegate to anti-slop before any completion claim.

You MUST delegate to phase-gate before moving to a later phase.

You MUST NOT accept self-certification from the same agent that authored the implementation.

## 11. Repair loop policy

When a case fails:

1. Run `.ai/hooks/on-failure.md`
2. Classify the failure as REAL FAIL, STUB, BLOCKED, or INVALID
3. Reduce to the smallest failing unit
4. Stay within allowed patch scope
5. Re-run the explicit Meson target
6. Re-run verifier and anti-slop
7. Stop when retry budget is exhausted

Do not widen scope casually.
Do not pivot to a new case to escape a failing one.
Do not continue if the active case contract is INVALID.

## 12. Truth before code

For semantic, decode, ABI, and ELF work, expected behavior MUST be written down before implementation changes.

Do not patch until the truth chain is explicit.

At minimum, `authority.yaml` MUST explain:
- what is normative
- what source defines the expectation
- what is stable vs unstable
- what is intentionally out of scope

## 13. Reality over appearances

The repository forbids fake pass patterns.

Examples of forbidden behavior:
- returning success from a stub harness
- writing placeholder artifacts only to satisfy file-existence checks
- simulating execution in a semantic execution case
- generating fake trace files on initialization failure and still claiming pass
- hardcoding vector data inside a harness when the case contract says expectations come from case files
- claiming "real enough" without the real path being exercised

If such a pattern exists, classify the result as STUB or INVALID, not PASS.

## 14. Meson-only policy

All cases MUST be wired explicitly in Meson.

No dynamic discovery.
No hidden dispatchers.
No shell-script public workflow in place of explicit build registration.

If a case exists but is not explicitly wired in Meson, it is not ready.

## 15. Phase gating

The phase order is defined in `tests/cases/execution-order.yaml`.

You MUST NOT proceed to a later phase while an earlier gate case is not REAL PASS.

If a later-phase task arrives early, reduce it to the earliest unsatisfied prerequisite case or create that missing case first.

## 16. Artifact discipline

Artifacts are part of the contract.

A case MUST define:
- which artifacts are required
- where they live
- what schema they follow
- which fields are normative
- what constitutes failure

Artifacts are not decoration. They are evidence.

## 17. Documentation discipline

When you discover or enforce a rule that future runs need, update `.ai/` documentation.

When a repo instruction becomes stale, fix it.
When a new case pattern repeats, document it.
When a review rule matters more than once, encode it.

Treat repository legibility as part of correctness.

## 18. Merge and claim policy

Before any "done", "fixed", or "pass" claim, run:
- verifier
- anti-slop
- review-skeptic
- phase-gate

If any of them disagrees, the claim is not ready.

Use exact status language.
Do not overclaim.
Do not hide uncertainty.
Do not call scaffolding completion.

## 19. Default posture

Be skeptical.
Prefer smaller units.
Prefer explicit contracts.
Prefer real execution paths.
Prefer deterministic fixtures.
Prefer repo-local truth over conversational memory.

Humans steer.
Agents execute.
Harnesses prove.
Rules keep the system honest.
