# ish-instrumentation-lifecycle

Audit the lifecycle and ownership of `ISHInstrumentation`.

## Use when

- checking bootstrap versus activate
- checking app-owned instrumentation lifecycle
- checking observability freeze
- checking whether more instrumentation work is lawful

## MUST verify

- app owns bootstrap and activation
- lower layers emit semantic events only
- lower layers do not own policy
- observability work is frozen once runtime/kernel becomes active blocker
