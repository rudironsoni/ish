# 75-profiling

Profiling is evidence-based performance optimization.

## When to profile

- Before optimizing: establish a baseline
- After code changes: measure impact
- When investigating performance regressions
- When adding new code paths (TB compilation, syscall handling)

## Required profiling steps

1. **Build with profiling**: `-Denable_profiling=true`
2. **Capture baseline**: Run workload before changes
3. **Make changes**: Implement optimization
4. **Capture comparison**: Run identical workload
5. **Analyze**: Use `ish-profile-tool.py compare`

## Profile requirements

A valid profile must:
- Capture the workload, not setup/teardown
- Include at least 1000 samples for statistical relevance
- Document the workload (command run, duration)
- Be reproducible (same workload = similar results)

## Key metrics

Focus on these for TCTI optimization:
- `TB_COMPILE` count and duration
- `TLB_MISS` rate (should be <5% of memory accesses)
- `TB_EXECUTE` cycles per block
- Memory allocation patterns

## Analysis workflow

```bash
# 1. Build
meson setup builddir -Denable_profiling=true

# 2. Baseline
ISH_PROFILE_OUTPUT=baseline.json ./builddir/ish -f alpine /bin/sh -c "workload"

# 3. Make changes...

# 4. Comparison
ISH_PROFILE_OUTPUT=changed.json ./builddir/ish -f alpine /bin/sh -c "workload"

# 5. Analyze
python3 tools/ish-profile-tool.py compare baseline.json changed.json
```

## Forbidden patterns

- Optimizing without profiling first
- Claiming improvements without profile comparison
- Ignoring regression warnings in profile diffs
- Profiling debug builds for performance claims

## Integration with cases

Performance-critical cases should:
- Define expected metrics in `expected.yaml`
- Include profile capture in test harness
- Specify acceptable variance (e.g., ±10%)
