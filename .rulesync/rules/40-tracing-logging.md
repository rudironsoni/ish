# Tracing and Logging Rules

## Single Observability Path

You MUST use only the approved instrumentation and trace system for all debugging, proof collection, and runtime investigation.

You MUST NOT create, keep, or use any second output path.

## Forbidden Output Mechanisms

You MUST NOT add, keep, or rely on any of the following in product or runtime code:

- printk
- printf
- fprintf
- NSLog
- direct os_log outside the approved sink
- stderr writes
- temporary file logging
- ad hoc debug dumps
- custom debug prefixes such as [mem], [SIGNAL], [FAULT-FIRST], [TCTI], [A64_*], or similar
- any equivalent bypass

## Mandatory Cleanup Rule

If any forbidden logging exists in code you touch or previously added, you MUST remove it.

You MUST replace needed visibility with proper semantic trace checkpoints through the approved instrumentation path only.

You MUST NOT leave forbidden logging in place "temporarily".

## Trace Design Rules

Every trace checkpoint MUST answer one specific proof question.

You MUST keep trace additions:
- semantic
- minimal
- boundary-focused
- tied to one exact hypothesis

You MUST NOT spray generic traces everywhere.

You MUST name checkpoints semantically, for example:
- task.proof.*
- session.*
- guest.*
- pty.*
- stdio.*

Use the real naming scheme already established in the repo.

## Trustworthiness Rule

Any conclusion derived from forbidden logging is contaminated and invalid.

If forbidden logging was used in a run, you MUST explicitly say that run is contaminated and cannot be used as proof.

## No Substitution Rule

You MUST NOT replace one forbidden logging method with another forbidden logging method.

If visibility is needed, the only allowed replacement is approved instrumentation.

## Change Isolation Rule

Logging cleanup and trace replacement MUST be kept separate from unrelated behavior changes whenever possible.

Do not hide behavior changes inside logging cleanup.

## Reporting Rule

Whenever you change tracing or logging, you MUST report exactly:
- which forbidden outputs were removed
- which files were changed
- which new approved trace checkpoints were added
- what proof question each new checkpoint answers
- confirmation that no forbidden debug output remains in the changed scope

## Ambiguity Rule

If you are unsure whether a log line is allowed, do not keep it silently.

Report it explicitly and ask for classification, or remove it if it is clearly ad hoc debugging.

## Enforcement

If you add forbidden logging again, your debugging results will be rejected.

Replace ad hoc logging with proper traces only.
