# iSH Tracing Subsystem

A first-class tracing and diagnostics system for the iSH AArch64 runtime.

## Overview

The tracing subsystem provides structured, runtime-configurable event tracing for debugging across all supported Linux distributions (Alpine, Ubuntu, etc.). It replaces ad-hoc debug prints with a unified flight recorder architecture.

## Quick Start

```bash
# Enable boundary-level tracing with ring buffer
ISH_TRACE_BACKEND=ring ISH_TRACE_LEVEL=2 ./ish

# Dump on fault to file
ISH_TRACE_BACKEND=ring ISH_TRACE_LEVEL=2 ISH_TRACE_DUMP_ON=fault \
  ISH_TRACE_OUT=/tmp/ish.trace.ring ./ish

# Decode the trace dump
python3 scripts/trace_decode.py /tmp/ish.trace.ring

# Decode to JSON for analysis
python3 scripts/trace_decode.py /tmp/ish.trace.ring -f json -o trace.json
```

## Environment Variables

### Core Configuration

| Variable | Values | Default | Description |
|----------|--------|---------|-------------|
| `ISH_TRACE_BACKEND` | `nop`, `ring`, `stderr` | `ring` | Trace output backend |
| `ISH_TRACE_LEVEL` | `0-5` | `0` (off) | Trace verbosity level |
| `ISH_TRACE_EVENTS` | comma-separated list | `all` | Events to enable |
| `ISH_TRACE_PC` | `start-end` | none | PC range filter |

### Advanced Configuration

| Variable | Values | Default | Description |
|----------|--------|---------|-------------|
| `ISH_TRACE_REGS` | comma-separated list | `x0-x5` | Registers to snapshot |
| `ISH_TRACE_RING_SIZE` | integer | `16384` | Ring buffer size in records |
| `ISH_TRACE_OUT` | path | none | Output file path |
| `ISH_TRACE_DUMP_ON` | `fault`, `exit` | none | Auto-dump triggers |
| `ISH_TRACE_MAX_EVENTS` | integer | unlimited | Stop after N events |

## Trace Levels

| Level | Name | Events |
|-------|------|--------|
| 0 | OFF | No tracing |
| 1 | SUMMARY | Counters, compile summaries, process entry |
| 2 | BOUNDARY | Block entry/exit, syscall boundaries, faults |
| 3 | BLOCK | Block sidecars, register snapshots, decoded instructions |
| 4 | INSTRUCTION | Per-instruction execution (scaffolded) |
| 5 | FORENSIC | Deep capture for one bug (scaffolded) |

## Event Families

### Compile and Decode
- `block.compile` - Block compilation events
- `decode` - Decode failure events

### Runtime Boundaries
- `block.entry` - Block execution entry
- `block.exit` - Block execution exit
- `block.cache` - Cache hit/miss events
- `fault` - Memory faults and exceptions
- `syscall` - Syscall entry/return

### Register State
- `register` - Register snapshots

## Usage Examples

### Boundary Tracing (Level 2)

```bash
# Trace all block boundaries and faults
ISH_TRACE_BACKEND=stderr ISH_TRACE_LEVEL=2 ./ish-alpine

# Trace only specific PC range
ISH_TRACE_BACKEND=ring ISH_TRACE_LEVEL=2 \
  ISH_TRACE_PC=0xf7fb0100-0xf7fb0200 \
  ISH_TRACE_DUMP_ON=fault ./ish-alpine
```

### Block-Level Tracing (Level 3)

```bash
# Capture block sidecars and register snapshots
ISH_TRACE_BACKEND=ring ISH_TRACE_LEVEL=3 \
  ISH_TRACE_REGS=x2,x3,x4,x5 \
  ISH_TRACE_OUT=/tmp/trace.ring ./ish-alpine

# Decode and inspect
tail -c 100K /tmp/trace.ring | python3 scripts/trace_decode.py - --last 20
```

### Specific Event Filtering

```bash
# Only trace faults and syscalls
ISH_TRACE_BACKEND=stderr ISH_TRACE_LEVEL=2 \
  ISH_TRACE_EVENTS=fault,syscall ./ish-alpine

# Only trace block compilation
ISH_TRACE_BACKEND=stderr ISH_TRACE_LEVEL=1 \
  ISH_TRACE_EVENTS=block.compile ./ish-alpine
```

## Agentic Debugging Workflow

This is the canonical debugging process for future bug investigations.

### Step 1: Define the Smallest Failing Unit

Before any tracing, identify exactly one:
- Single instruction
- Two-instruction sequence
- Tiny basic block
- Process-entry fixture
- Syscall fixture
- Narrow runtime boundary proof

**Example:** "The `stp x3, x4, [sp, #24]` instruction at PC 0xf7fb0130 corrupts guest x4."

### Step 2: State Expected Truth

Document the expected behavior:
```
ISA semantics: STP (store pair) should store x3 and x4 to memory
Expected: Guest x4 should remain 0xfffffd10
Expected: Memory at [sp+24] should contain x4 value
Actual: Guest x4 becomes 0
First failing layer: Execution (registers corrupted during store)
```

### Step 3: Run with Boundary Tracing (Level 2)

```bash
# First pass: boundary tracing with ring buffer
ISH_TRACE_BACKEND=ring \
  ISH_TRACE_LEVEL=2 \
  ISH_TRACE_DUMP_ON=fault \
  ISH_TRACE_PC=0xf7fb0100-0xf7fb0200 \
  ./tests/aarch64/test_prefix_0xf7fb0130

# Decode the trace
python3 scripts/trace_decode.py /tmp/ish.trace.ring -f text --last 20
```

**What to look for:**
- Block entry/exit events
- Fault events
- Syscall boundaries
- Cache hits/misses

### Step 4: Escalate to Block Level (Level 3) if Needed

If boundary tracing is insufficient:

```bash
# Enable block-level tracing with sidecars
ISH_TRACE_BACKEND=ring \
  ISH_TRACE_LEVEL=3 \
  ISH_TRACE_PC=0xf7fb0130-0xf7fb0140 \
  ISH_TRACE_REGS=x2,x3,x4 \
  ISH_TRACE_OUT=/tmp/trace.ring \
  ./tests/aarch64/test_prefix_0xf7fb0130

# The sidecar will be dumped on fault or can be decoded from the trace
python3 scripts/trace_decode.py /tmp/trace.ring -f text
```

**What to look for:**
- Block sidecar with instruction list
- Register snapshots before/after blocks
- Decoded instruction metadata

### Step 5: Escalate to Instruction Level (Level 4) if Needed

Only if still insufficient:

```bash
# Narrow PC range for instruction-level tracing
ISH_TRACE_BACKEND=stderr \
  ISH_TRACE_LEVEL=4 \
  ISH_TRACE_PC=0xf7fb0130-0xf7fb0134 \
  ISH_TRACE_EVENTS=instruction.decode \
  ./tests/aarch64/test_prefix_0xf7fb0130 2>&1 | head -100
```

### Step 6: Analyze with Offline Tools

```bash
# Full decode to JSON for programmatic analysis
python3 scripts/trace_decode.py /tmp/trace.ring -f json -o trace.json

# Filter specific events
python3 scripts/trace_decode.py /tmp/trace.ring \
  --filter-event FAULT --format text

# Show only high-level events
python3 scripts/trace_decode.py /tmp/trace.ring \
  --min-level 2 --format text
```

### Step 7: Patch Only After Failing Layer Proven

Do NOT patch until you have proven:
1. ISA truth - instruction semantics are correct
2. Decoder truth - fields decoded correctly
3. Generator truth - correct gadgets emitted
4. Execution truth - actual runtime behavior
5. First failing layer identified

**Patch target:** Exactly one file, one function.

### Step 8: Add Regression Test

```c
// tests/trace/test_regression_xyz.c
// Reproducer that runs under tracing and validates fix
```

## Ring Buffer Format

The ring buffer uses a compact binary format:

```
Dump Header (40 bytes):
  - magic: "ISH\0" (4 bytes)
  - version: 1 (2 bytes)
  - header_size: 40 (2 bytes)
  - endianness: 1=little (1 byte)
  - record_header_size: 24 (1 byte)
  - max_payload_size: 48 (2 bytes)
  - record_count (8 bytes)
  - dropped_count (8 bytes)
  - start_seq (8 bytes)

Records (64 bytes each):
  - seq: sequence number (8 bytes)
  - event_id (1 byte)
  - level (1 byte)
  - cpu_id (2 bytes)
  - pc (8 bytes)
  - payload_size (2 bytes)
  - payload[48] (48 bytes)
```

## Block Sidecar

Block sidecars provide debug metadata for compiled blocks:

- Start/end PC
- Instruction count
- Per-instruction metadata (PC, raw bytes, mnemonic)
- Register liveness information

Sidecars are automatically created when `ISH_TRACE_LEVEL >= 3` or `ISH_TRACE_DUMP_ON` is set.

## Implementation Notes

### Backend Selection

- **NOP**: Zero overhead, no events recorded
- **RING**: Default, circular buffer flight recorder
- **STDERR**: Immediate human-readable output

### Performance

- Ring buffer emission: ~50-100ns per event
- Event filtering done before emission
- No formatting in hot path
- Sidecars allocated lazily

### Thread Safety

Current implementation uses single global trace context.
For multi-threaded scenarios, use per-CPU ring buffers (future enhancement).

## Troubleshooting

### No trace output

```bash
# Check level is > 0
echo $ISH_TRACE_LEVEL

# Check backend is not nop
echo $ISH_TRACE_BACKEND

# Verify trace_init() was called
# (Should happen automatically in a64_cpu_run())
```

### Ring buffer not dumping

```bash
# Ensure ISH_TRACE_DUMP_ON is set
export ISH_TRACE_DUMP_ON=fault

# Check output path is writable
touch $ISH_TRACE_OUT 2>&1
```

### Decode script errors

```bash
# Verify Python 3.7+
python3 --version

# Check file is not empty
ls -la /tmp/ish.trace.ring

# Check file magic
xxd /tmp/ish.trace.ring | head -1
```

## Future Enhancements

The following are planned but not implemented:

- JSONL backend for live streaming
- Perfetto/Chrome trace export
- Replay capability
- External analyzer plugins
- Gadget-level instruction tracing
- Compressed trace format

## Architecture

```
┌─────────────────────────────────────┐
│        User Application             │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│      Trace API (trace.h)            │
│  TRACE(), trace_emit(), etc.        │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│    Trace Core (trace.c)             │
│  Filtering, dispatch, state         │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│   Backend Dispatch                  │
├──────────────┬──────────────┬───────┤
│     NOP      │    RING      │STDERR │
│  (no-op)     │  (buffer)    │(print)│
└──────────────┴──────────────┴───────┘
```

## See Also

- `trace/trace.h` - Public API
- `trace/trace_types.h` - Type definitions
- `scripts/trace_decode.py` - Offline decoder
- `tests/trace/` - Test fixtures
