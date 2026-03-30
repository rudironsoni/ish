# ish-runtime-reduction

Reduce runtime failures to one exact boundary.

## Use when

- guest runtime is under test
- runtime/kernel is the active blocker
- exact failing edge is not yet proven

## MUST produce

- current enabled runtime scope
- last known good point
- first known bad point
- exact failing edge

## MUST reject

- broad narratives
- multiple boundary reintroductions at once
- unproven root-cause claims
