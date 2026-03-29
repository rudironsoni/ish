# iSH Profiling Infrastructure

This document describes the profiling infrastructure for iSH, enabling performance analysis and optimization of the TCTI emulator.

## Overview

The profiling system provides multiple backends for performance analysis:

1. **Statistical Counters** - Fast increment-only counters for hot paths
2. **JSON Profile Output** - Structured event recording for detailed analysis
3. **Flame Graph Generation** - Visualization of execution patterns
4. **Continuous Profiling** - Periodic sampling for long-running workloads

## Building with Profiling

```bash
# Configure with profiling enabled
meson setup builddir -Denable_profiling=true

# Or use just
just build-prof
```

## Usage

### Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `ISH_PROFILE_OUTPUT` | Path to profile JSON output | `ish-profile.json` |
| `ISH_PROFILE_AUTO` | Auto-start profiling on init | (unset) |
| `ISH_PROFILE_INTERVAL_MS` | Continuous sampling interval | `1000` |

### Using the Just Commands

```bash
# Build with profiling
just build-prof

# Run and profile
just profile-run "-f alpine /bin/sh"

# Analyze results
just profile-analyze ish-profile.json

# Generate flame graph data
just profile-flame ish-profile.json flame.txt
```

### Programmatic Usage

```c
#include "util/prof.h"

// Initialize profiling
prof_init("output.json");

// Capture sample events
PROF_CAPTURE(PROF_EVENT_SAMPLE);

// Time a block of code
uint64_t handle = PROF_BEGIN(PROF_EVENT_TB_COMPILE);
compile_translation_block();
PROF_END(handle, PROF_EVENT_TB_COMPILE);

// Record allocations
void *ptr = malloc(size);
PROF_RECORD_ALLOC(ptr, size);

// Record TLB misses
PROF_RECORD_TLB_MISS(vaddr, is_write, level);

// Shutdown and flush
prof_shutdown();
```

## Event Types

| Event | Description | Data Fields |
|-------|-------------|-------------|
| `PROF_EVENT_SAMPLE` | Periodic CPU sample | timestamp, thread |
| `PROF_EVENT_ALLOC` | Memory allocation | ptr, size, func |
| `PROF_EVENT_FREE` | Memory free | ptr |
| `PROF_EVENT_SYSCALL` | System call | syscall_no, arg0 |
| `PROF_EVENT_TB_COMPILE` | TB compilation | guest_ip, insn_count, code_size |
| `PROF_EVENT_TB_EXECUTE` | TB execution | guest_ip, cycles |
| `PROF_EVENT_INTERRUPT` | Interrupt handling | - |
| `PROF_EVENT_TLB_MISS` | TLB miss | vaddr, is_write, level |
| `PROF_EVENT_BLOCK_CACHE` | Block cache operation | - |

## Profile Analysis

### Using ish-profile-tool

```bash
# Analyze profile
./tools/ish-profile-tool.py analyze ish-profile.json

# Generate flame graph
./tools/ish-profile-tool.py flamegraph ish-profile.json > flame.txt

# Compare two profiles
./tools/ish-profile-tool.py compare baseline.json current.json
```

### Flame Graph Visualization

Generate flame graph data and visualize with:

- [Speedscope](https://www.speedscope.app/) - Web-based viewer
- [FlameGraph](https://github.com/brendangregg/FlameGraph) - Command line tools

```bash
# Generate SVG flame graph (requires FlameGraph tools)
./tools/ish-profile-tool.py flamegraph profile.json | \
    flamegraph.pl --title "iSH Profile" > profile.svg
```

## Performance Impact

Profiling has minimal overhead when disabled (zero-cost macros). When enabled:

- **Statistical counters**: ~1-2% overhead
- **JSON output**: ~5-10% overhead (depends on sample rate)
- **Continuous profiling**: ~3-5% overhead (1ms intervals)

## Integration with Existing Stats

The profiling system complements the existing `enable_perf_stats` option:

- `enable_perf_stats`: Fast counters for optimization work (always available)
- `enable_profiling`: Detailed event tracing for analysis (optional)

Both can be enabled simultaneously:

```bash
meson setup builddir -Denable_perf_stats=true -Denable_profiling=true
```

## Best Practices

1. **Profile in release builds** for accurate performance data
2. **Use short durations** for targeted profiling (10-30 seconds)
3. **Focus on TB execution** for emulator optimization
4. **Monitor TLB miss rates** to validate memory optimizations
5. **Compare profiles** before/after changes to measure impact

## Example Workflows

### Optimizing Translation Block Cache

```bash
# Profile workload
ISH_PROFILE_OUTPUT=baseline.json ./ish -f alpine /bin/sh -c "stress test"

# Make code changes...

# Profile again
ISH_PROFILE_OUTPUT=optimized.json ./ish -f alpine /bin/sh -c "stress test"

# Compare
./tools/ish-profile-tool.py compare baseline.json optimized.json
```

### Analyzing TLB Performance

```bash
# Profile with TLB focus
ISH_PROFILE_OUTPUT=tlb.json ./ish -f alpine /bin/sh

# Analyze TLB miss patterns
./tools/ish-profile-tool.py analyze tlb.json
# Look for: TLB Miss Analysis section
```

## Continuous Profiling

For long-running workloads, enable continuous profiling:

```c
// Start periodic sampling (every 100ms)
prof_start_continuous(100);

// ... run workload ...

// Stop and analyze
prof_stop_continuous();
prof_dump_stats();
```

## JSON Output Format

```json
{
  "version": 1,
  "start_time": 1234567890123456789,
  "samples": [
    {
      "timestamp": 1234567890123456790,
      "thread": 0,
      "type": 4,
      "guest_ip": "0x40005000",
      "insn_count": 50,
      "code_size": 1024
    }
  ],
  "summary": {
    "total_samples": 10000,
    "duration_ns": 10000000000,
    "duration_ms": 10000.00,
    "samples_per_second": 1000.00
  }
}
```

## Troubleshooting

### No profile output

- Check that profiling is enabled: `-Denable_profiling=true`
- Verify `ISH_PROFILE_OUTPUT` path is writable
- Ensure `prof_init()` is called before sampling

### High overhead

- Reduce sample frequency: increase `ISH_PROFILE_INTERVAL_MS`
- Filter events: comment out `PROF_CAPTURE` calls for less critical events
- Use release builds: `meson configure builddir --buildtype=release`

### Large output files

- Enable buffering: samples are written in 16MB chunks
- Compress output: `gzip ish-profile.json`
- Reduce profiling duration: focus on specific workloads
