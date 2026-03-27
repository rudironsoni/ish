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
