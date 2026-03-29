#!/usr/bin/env python3
"""
iSH Profile Analysis Tool

Analyzes JSON profile output from the iSH profiling infrastructure.

Usage:
    ./tools/ish-profile-tool.py analyze profile.json
    ./tools/ish-profile-tool.py flamegraph profile.json > flame.txt
    ./tools/ish-profile-tool.py stats profile.json

Environment:
    ISH_PROFILE_OUTPUT    - Default profile file path
"""

import json
import sys
import argparse
from collections import defaultdict
from pathlib import Path


def load_profile(path: str) -> dict:
    """Load profile JSON file."""
    with open(path, 'r') as f:
        return json.load(f)


def cmd_analyze(args):
    """Analyze profile and print summary."""
    profile = load_profile(args.profile)
    
    samples = profile.get('samples', [])
    summary = profile.get('summary', {})
    
    print("=" * 60)
    print("iSH Profile Analysis")
    print("=" * 60)
    
    print(f"\nTotal samples: {len(samples)}")
    print(f"Duration: {summary.get('duration_ms', 0):.2f} ms")
    print(f"Sample rate: {summary.get('samples_per_second', 0):.2f} samples/sec")
    
    # Count by event type
    event_types = defaultdict(int)
    for sample in samples:
        event_type = sample.get('type', 'unknown')
        event_types[event_type] += 1
    
    print("\n--- Event Distribution ---")
    type_names = {
        0: 'SAMPLE',
        1: 'ALLOC',
        2: 'FREE',
        3: 'SYSCALL',
        4: 'TB_COMPILE',
        5: 'TB_EXECUTE',
        6: 'INTERRUPT',
        7: 'TLB_MISS',
        8: 'BLOCK_CACHE'
    }
    
    for type_id, count in sorted(event_types.items(), key=lambda x: -x[1]):
        name = type_names.get(type_id, f'TYPE_{type_id}')
        pct = count / len(samples) * 100 if samples else 0
        print(f"  {name:20s}: {count:6d} ({pct:5.1f}%)")
    
    # TLB miss analysis
    tlb_misses = [s for s in samples if s.get('type') == 7]
    if tlb_misses:
        print("\n--- TLB Miss Analysis ---")
        reads = sum(1 for s in tlb_misses if not s.get('is_write', False))
        writes = sum(1 for s in tlb_misses if s.get('is_write', False))
        print(f"  Read misses:  {reads}")
        print(f"  Write misses: {writes}")
    
    # TB compilation analysis
    tb_compiles = [s for s in samples if s.get('type') == 4]
    if tb_compiles:
        print("\n--- Translation Block Compilation ---")
        print(f"  Total compiles: {len(tb_compiles)}")
        total_insns = sum(s.get('insn_count', 0) for s in tb_compiles)
        total_code = sum(s.get('code_size', 0) for s in tb_compiles)
        print(f"  Total instructions: {total_insns}")
        print(f"  Total code size: {total_code} bytes")
        if tb_compiles:
            print(f"  Avg insn/block: {total_insns / len(tb_compiles):.1f}")
            print(f"  Avg code size: {total_code / len(tb_compiles):.1f} bytes")
    
    # Memory allocation analysis
    allocs = [s for s in samples if s.get('type') == 1]
    frees = [s for s in samples if s.get('type') == 2]
    if allocs or frees:
        print("\n--- Memory Allocation ---")
        total_alloc = sum(s.get('size', 0) for s in allocs)
        print(f"  Allocations: {len(allocs)}")
        print(f"  Frees: {len(frees)}")
        print(f"  Total allocated: {total_alloc} bytes ({total_alloc / 1024 / 1024:.2f} MB)")
        if len(allocs) != len(frees):
            print(f"  WARNING: Potential memory leak (diff: {len(allocs) - len(frees)})")
    
    # Timed events
    timed_samples = [s for s in samples if 'duration_ns' in s]
    if timed_samples:
        print("\n--- Timed Events ---")
        total_duration = sum(s['duration_ns'] for s in timed_samples)
        avg_duration = total_duration / len(timed_samples)
        max_duration = max(s['duration_ns'] for s in timed_samples)
        print(f"  Events: {len(timed_samples)}")
        print(f"  Total time: {total_duration / 1e6:.2f} ms")
        print(f"  Average: {avg_duration / 1000:.2f} us")
        print(f"  Maximum: {max_duration / 1000:.2f} us")
    
    print("\n" + "=" * 60)


def cmd_flamegraph(args):
    """Generate flame graph data."""
    profile = load_profile(args.profile)
    samples = profile.get('samples', [])
    
    # Build folded stacks (simplified - just event types)
    stacks = defaultdict(int)
    
    for sample in samples:
        event_type = sample.get('type', 0)
        type_names = {
            0: 'sample',
            1: 'alloc',
            2: 'free',
            3: 'syscall',
            4: 'tb_compile',
            5: 'tb_execute',
            6: 'interrupt',
            7: 'tlb_miss',
            8: 'block_cache'
        }
        
        stack_name = type_names.get(event_type, f'type_{event_type}')
        
        # Add detail for certain types
        if event_type == 7 and 'is_write' in sample:  # TLB miss
            access = 'write' if sample['is_write'] else 'read'
            stack_name = f'tlb_miss;{access}'
        elif event_type == 1 and 'func' in sample:  # Alloc
            func = sample['func']
            stack_name = f'alloc;{func}'
        
        stacks[stack_name] += 1
    
    # Output folded format
    for stack, count in sorted(stacks.items(), key=lambda x: -x[1]):
        print(f"{stack} {count}")


def cmd_stats(args):
    """Print raw statistics."""
    profile = load_profile(args.profile)
    summary = profile.get('summary', {})
    
    print(json.dumps(summary, indent=2))


def cmd_compare(args):
    """Compare two profiles."""
    profile1 = load_profile(args.profile)
    profile2 = load_profile(args.baseline)
    
    samples1 = profile1.get('samples', [])
    samples2 = profile2.get('samples', [])
    
    print("=" * 60)
    print("Profile Comparison")
    print("=" * 60)
    print(f"\nBaseline: {args.baseline}")
    print(f"Current:  {args.profile}")
    
    # Count by event type
    def count_types(samples):
        counts = defaultdict(int)
        for s in samples:
            counts[s.get('type', 'unknown')] += 1
        return counts
    
    counts1 = count_types(samples1)
    counts2 = count_types(samples2)
    
    all_types = set(counts1.keys()) | set(counts2.keys())
    
    print("\n--- Event Comparison ---")
    print(f"{'Type':<15} {'Baseline':>10} {'Current':>10} {'Diff':>10} {'% Change':>10}")
    print("-" * 60)
    
    for type_id in sorted(all_types):
        c1 = counts1.get(type_id, 0)
        c2 = counts2.get(type_id, 0)
        diff = c2 - c1
        
        if c1 > 0:
            pct = (diff / c1) * 100
            pct_str = f"{pct:+.1f}%"
        elif c2 > 0:
            pct_str = "+inf%"
        else:
            pct_str = "0%"
        
        type_names = {
            0: 'SAMPLE', 1: 'ALLOC', 2: 'FREE', 3: 'SYSCALL',
            4: 'TB_COMPILE', 5: 'TB_EXECUTE', 6: 'INTERRUPT',
            7: 'TLB_MISS', 8: 'BLOCK_CACHE'
        }
        name = type_names.get(type_id, f'TYPE_{type_id}')
        
        print(f"{name:<15} {c1:>10} {c2:>10} {diff:+>10} {pct_str:>10}")
    
    print("\n" + "=" * 60)


def main():
    parser = argparse.ArgumentParser(
        description='iSH Profile Analysis Tool',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  %(prog)s analyze profile.json           # Analyze profile
  %(prog)s flamegraph profile.json > fg   # Generate flame graph data
  %(prog)s stats profile.json             # Show raw stats
  %(prog)s compare baseline.json profile.json  # Compare profiles
        '''
    )
    
    subparsers = parser.add_subparsers(dest='command', required=True)
    
    # analyze
    analyze_parser = subparsers.add_parser('analyze', help='Analyze profile')
    analyze_parser.add_argument('profile', help='Profile JSON file')
    
    # flamegraph
    flame_parser = subparsers.add_parser('flamegraph', help='Generate flame graph')
    flame_parser.add_argument('profile', help='Profile JSON file')
    
    # stats
    stats_parser = subparsers.add_parser('stats', help='Show raw statistics')
    stats_parser.add_argument('profile', help='Profile JSON file')
    
    # compare
    compare_parser = subparsers.add_parser('compare', help='Compare two profiles')
    compare_parser.add_argument('baseline', help='Baseline profile')
    compare_parser.add_argument('profile', help='Current profile')
    
    args = parser.parse_args()
    
    # Default to env var if profile not specified
    if hasattr(args, 'profile') and args.profile is None:
        args.profile = os.environ.get('ISH_PROFILE_OUTPUT', 'ish-profile.json')
    
    try:
        if args.command == 'analyze':
            cmd_analyze(args)
        elif args.command == 'flamegraph':
            cmd_flamegraph(args)
        elif args.command == 'stats':
            cmd_stats(args)
        elif args.command == 'compare':
            cmd_compare(args)
    except FileNotFoundError as e:
        print(f"Error: File not found: {e.filename}", file=sys.stderr)
        sys.exit(1)
    except json.JSONDecodeError as e:
        print(f"Error: Invalid JSON: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
