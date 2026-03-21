# iSH Performance Baseline Results

This directory contains baseline performance measurements for iSH on different iOS devices.

## File Naming Convention

```
{DEVICE_NAME}_{YYYYMMDD}_{HHMMSS}.json
```

Example: `iPhone_15_Pro_Max_20240321_143022.json`

## How to Collect Baseline

### 1. Build with Performance Stats Enabled

```bash
meson setup build --buildtype=debug -Denable_perf_stats=true
ninja -C build
```

### 2. Run Baseline Script

```bash
./tests/performance/run_baseline.sh "iPhone_15_Pro_Max"
```

Or for other devices:
```bash
./tests/performance/run_baseline.sh "iPad_9th"
./tests/performance/run_baseline.sh "iPhone_SE"
```

### 3. Commit Results

```bash
git add results/baseline/
git commit -m "Add performance baseline for [Device Name]"
```

## Required Devices

We need baseline results from:

1. **iPhone SE (2016)** - A9 chip - Minimum viable performance target
2. **iPad 9th (2021)** - A13 chip - Mid-range target  
3. **iPhone 15 Pro Max** - A17 Pro chip - High-performance target

## Target Metrics

| Metric | iPhone SE | iPad 9th | iPhone 15 Pro |
|--------|-----------|----------|---------------|
| Shell startup | <3s | <1.5s | <1s |
| Loop 10k iterations | <5s | <2s | <1s |
| TB cache hit rate | >95% | >97% | >98% |
| TLB hit rate | >97% | >98% | >99% |

## Optimization Phases

After collecting baseline, we'll proceed with:

- **Phase 1**: Lock removal (asbestos->lock, futex_lock)
- **Phase 2**: Memory optimization (per-thread TLB)
- **Phase 3**: Testing & validation

Each phase will be measured against these baselines to ensure zero regressions.

## JSON Format

```json
{
  "device": "iPhone_15_Pro_Max",
  "timestamp": "2024-03-21T14:30:22Z",
  "git_commit": "abc123",
  "system": {
    "machine": "arm64",
    "system": "Darwin"
  },
  "cpu": {
    "cores": 6,
    "memory_bytes": 8589934592
  },
  "tests": {
    "shell_startup_ns": 850000000,
    "shell_startup_sec": 0.85,
    "loop_10k_ns": 950000000,
    "loop_10k_sec": 0.95
  }
}
```
