---
name: profiling
description: "Performance profiling for iSH TCTI emulator - capture profiles, analyze bottlenecks, compare results"
targets: ["*.c", "*.h"]
allowed-tools: ["Bash", "Read", "Edit"]
---

# iSH Profiling Skill

Use this skill when working on performance optimization, investigating bottlenecks, or validating code changes in the TCTI emulator.

## Quick commands

```bash
# Build with profiling
meson setup builddir -Denable_profiling=true --reconfigure
ninja -C builddir

# Run and capture profile
ISH_PROFILE_OUTPUT=profile.json ./builddir/ish -f alpine /bin/sh -c "your workload"

# Analyze
python3 tools/ish-profile-tool.py analyze profile.json

# Generate flame graph
python3 tools/ish-profile-tool.py flamegraph profile.json > flame.txt

# Compare profiles
python3 tools/ish-profile-tool.py compare baseline.json current.json
```

## Event types to capture

| Event | Use when optimizing... |
|-------|------------------------|
| `TB_COMPILE` | Translation block compilation speed |
| `TB_EXECUTE` | Block execution throughput |
| `TLB_MISS` | Memory access patterns |
| `ALLOC/FREE` | Memory allocation overhead |
| `SYSCALL` | Syscall dispatch performance |

## Code instrumentation

Add profiling to your code:

```c
#include "util/prof.h"

// Capture sample
PROF_CAPTURE(PROF_EVENT_SAMPLE);

// Time a block
uint64_t handle = PROF_BEGIN(PROF_EVENT_TB_COMPILE);
compile_block();
PROF_END(handle, PROF_EVENT_TB_COMPILE);

// Record allocation
void *ptr = malloc(size);
PROF_RECORD_ALLOC(ptr, size);
```

## Analysis workflow

1. **Establish baseline** - Profile before changes
2. **Make changes** - Implement optimization
3. **Profile again** - Same workload, new capture
4. **Compare** - Use `ish-profile-tool.py compare`
5. **Validate** - Confirm improvement, no regressions

## Key thresholds

- TLB miss rate: <5% (memory-bound workloads)
- TB compile time: <100us average
- Allocations in hot path: 0 (steady-state)

## Output files

- `profile.json` - Raw profile data
- `flame.txt` - Folded stack data for speedscope.app

See `docs/profiling.md` for complete documentation.
