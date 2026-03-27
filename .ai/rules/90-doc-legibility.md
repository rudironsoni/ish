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
