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
