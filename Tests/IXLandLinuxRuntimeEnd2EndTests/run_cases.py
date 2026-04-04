#!/usr/bin/env python3
"""
IXLand Runtime Case Corpus Runner

Discovers, validates, and reports on the E2E case corpus.
Does NOT execute C harness binaries (those require Linux/Meson).
Instead validates case structure, reports inventory, and tracks status.

Usage:
    python3 run_cases.py [--cases-dir PATH] [--verbose]
"""

import os
import sys
import yaml
import argparse
from pathlib import Path
from dataclasses import dataclass, field
from typing import Optional


REQUIRED_FIELDS = [
    "id",
    "phase",
    "title",
    "class",
    "kind",
    "harness",
    "meson_test",
    "prerequisites",
    "expectation_authority",
    "success_criteria",
    "allowed_patch_scope",
    "regression_target",
]

VALID_PHASES = {
    "00-trace-harness",
    "01-decode",
    "02-generator",
    "02b-ios-simulator-harness",
    "02c-ios-app-runtime-entry",
    "03-semantic-exec",
    "04-mmu-abi",
    "05-elf-loader",
    "06-syscalls-core-fs-net",
    "07-threads-signals-tls",
    "08-musl",
    "09-glibc",
    "10-tooling-stability-ios",
    "11-distro-matrix",
}


@dataclass
class CaseResult:
    case_id: str
    phase: str
    title: str
    harness: str
    kind: str
    valid: bool
    errors: list = field(default_factory=list)


@dataclass
class Summary:
    total_discovered: int = 0
    total_valid: int = 0
    total_invalid: int = 0
    by_phase: dict = field(default_factory=dict)
    by_harness: dict = field(default_factory=dict)
    cases: list = field(default_factory=list)


def validate_case(case_dir: Path, cases_root: Path) -> CaseResult:
    """Validate a single case directory."""
    case_file = case_dir / "case.yaml"
    errors = []

    if not case_file.exists():
        return CaseResult(
            case_id=case_dir.name,
            phase=case_dir.parent.name,
            title="",
            harness="",
            kind="",
            valid=False,
            errors=["Missing case.yaml"],
        )

    try:
        with open(case_file) as f:
            data = yaml.safe_load(f)
    except yaml.YAMLError as e:
        return CaseResult(
            case_id=case_dir.name,
            phase=case_dir.parent.name,
            title="",
            harness="",
            kind="",
            valid=False,
            errors=[f"YAML parse error: {e}"],
        )

    if not isinstance(data, dict):
        return CaseResult(
            case_id=case_dir.name,
            phase=case_dir.parent.name,
            title="",
            harness="",
            kind="",
            valid=False,
            errors=["case.yaml is not a mapping"],
        )

    # Check required fields
    for rf in REQUIRED_FIELDS:
        if rf not in data:
            errors.append(f"Missing required field: {rf}")

    # Validate phase
    phase = data.get("phase", "")
    if phase and phase not in VALID_PHASES:
        errors.append(f"Unknown phase: {phase}")

    # Validate ID format
    case_id = data.get("id", "")
    if case_id and not all(c.isupper() or c.isdigit() or c == "-" for c in case_id):
        errors.append(f"Invalid ID format: {case_id}")

    return CaseResult(
        case_id=data.get("id", case_dir.name),
        phase=phase,
        title=data.get("title", ""),
        harness=data.get("harness", ""),
        kind=data.get("kind", ""),
        valid=len(errors) == 0,
        errors=errors,
    )


def discover_cases(cases_root: Path) -> Summary:
    """Walk the case corpus and validate each case."""
    summary = Summary()

    if not cases_root.exists():
        print(f"ERROR: Cases directory not found: {cases_root}", file=sys.stderr)
        sys.exit(1)

    # Walk phase directories
    for phase_dir in sorted(cases_root.iterdir()):
        if not phase_dir.is_dir():
            continue
        if phase_dir.name in ("harness", "manual", "benchmark", "workloads"):
            continue
        if not phase_dir.name.startswith(("0", "1")):
            continue

        phase_name = phase_dir.name
        phase_count = 0

        for case_dir in sorted(phase_dir.iterdir()):
            if not case_dir.is_dir():
                continue

            result = validate_case(case_dir, cases_root)
            summary.total_discovered += 1
            summary.cases.append(result)

            if result.valid:
                summary.total_valid += 1
            else:
                summary.total_invalid += 1

            # Track by phase
            if phase_name not in summary.by_phase:
                summary.by_phase[phase_name] = {"total": 0, "valid": 0, "invalid": 0}
            summary.by_phase[phase_name]["total"] += 1
            if result.valid:
                summary.by_phase[phase_name]["valid"] += 1
            else:
                summary.by_phase[phase_name]["invalid"] += 1

            # Track by harness
            harness = result.harness or "unknown"
            if harness not in summary.by_harness:
                summary.by_harness[harness] = 0
            summary.by_harness[harness] += 1

            phase_count += 1

    return summary


def print_report(summary: Summary, verbose: bool = False):
    """Print the case corpus report."""
    print("=" * 70)
    print("IXLand Runtime Case Corpus Report")
    print("=" * 70)
    print()

    print(f"Total cases discovered: {summary.total_discovered}")
    print(f"  Valid:   {summary.total_valid}")
    print(f"  Invalid: {summary.total_invalid}")
    print()

    print("By Phase:")
    print(f"  {'Phase':<35} {'Total':>6} {'Valid':>6} {'Invalid':>8}")
    print(f"  {'-' * 35} {'-' * 6} {'-' * 6} {'-' * 8}")
    for phase in sorted(summary.by_phase.keys()):
        info = summary.by_phase[phase]
        print(
            f"  {phase:<35} {info['total']:>6} {info['valid']:>6} {info['invalid']:>8}"
        )
    print()

    print("By Harness Type:")
    for harness in sorted(summary.by_harness.keys()):
        print(f"  {harness:<30} {summary.by_harness[harness]:>4}")
    print()

    if verbose and summary.cases:
        print("Case Details:")
        for c in summary.cases:
            status = "VALID" if c.valid else "INVALID"
            print(f"  [{status}] {c.case_id} ({c.phase}) - {c.harness}")
            if c.errors:
                for err in c.errors:
                    print(f"           ERROR: {err}")
        print()

    print("=" * 70)
    if summary.total_invalid > 0:
        print(f"RESULT: {summary.total_invalid} case(s) failed validation")
        sys.exit(1)
    else:
        print(f"RESULT: All {summary.total_discovered} cases passed validation")
        sys.exit(0)


def main():
    parser = argparse.ArgumentParser(description="IXLand Runtime Case Corpus Runner")
    parser.add_argument(
        "--cases-dir",
        type=str,
        default=None,
        help="Path to cases directory (default: auto-detect)",
    )
    parser.add_argument(
        "--verbose",
        "-v",
        action="store_true",
        help="Show detailed case information",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Output report as JSON",
    )
    args = parser.parse_args()

    # Auto-detect cases directory
    if args.cases_dir:
        cases_root = Path(args.cases_dir)
    else:
        # Try relative to this script
        script_dir = Path(__file__).parent
        cases_root = script_dir / "cases"
        if not cases_root.exists():
            # Try from repo root
            cases_root = (
                Path.cwd() / "tests" / "IXLandLinuxRuntimeEnd2EndTests" / "cases"
            )

    summary = discover_cases(cases_root)

    if args.json:
        import json

        report = {
            "total_discovered": summary.total_discovered,
            "total_valid": summary.total_valid,
            "total_invalid": summary.total_invalid,
            "by_phase": summary.by_phase,
            "by_harness": summary.by_harness,
        }
        print(json.dumps(report, indent=2))
        sys.exit(1 if summary.total_invalid > 0 else 0)
    else:
        print_report(summary, verbose=args.verbose)


if __name__ == "__main__":
    main()
