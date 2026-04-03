#!/usr/bin/env python3
"""
Case schema validation helper.

This is a CI helper, developer validation helper, and optional pre-test validator.
It is NOT a test dispatcher, test executor, or build system.

Usage:
    python3 case_schema.py --validate <case-dir>
    python3 case_schema.py --validate-all
"""

import sys
import yaml
from pathlib import Path
from typing import List, Optional

SCHEMA_FILE = Path(__file__).parent.parent / "schema.yaml"
EXECUTION_ORDER_FILE = Path(__file__).parent.parent / "execution-order.yaml"


def load_schema() -> dict:
    """Load the schema definition."""
    with open(SCHEMA_FILE, "r") as f:
        return yaml.safe_load(f)


def load_execution_order() -> dict:
    """Load the execution order definition."""
    with open(EXECUTION_ORDER_FILE, "r") as f:
        return yaml.safe_load(f)


def validate_case_yaml(case_dir: Path, schema: dict) -> List[str]:
    """Validate a single case directory."""
    errors = []

    case_yaml_path = case_dir / "case.yaml"
    if not case_yaml_path.exists():
        errors.append(f"Missing case.yaml in {case_dir}")
        return errors

    try:
        with open(case_yaml_path, "r") as f:
            case = yaml.safe_load(f)
    except yaml.YAMLError as e:
        errors.append(f"Invalid YAML in {case_yaml_path}: {e}")
        return errors

    # Check required fields
    required = schema.get("required_fields", [])
    for field in required:
        if field not in case:
            errors.append(f"Missing required field '{field}' in {case_yaml_path}")

    # Validate field values
    if "id" in case:
        import re

        if not re.match(r"^[A-Z]{2,6}-[0-9]{3}$", case["id"]):
            errors.append(f"Invalid id format '{case['id']}' in {case_yaml_path}")

    if "class" in case:
        valid_classes = (
            schema.get("field_definitions", {}).get("class", {}).get("values", [])
        )
        if case["class"] not in valid_classes:
            errors.append(f"Invalid class '{case['class']}' in {case_yaml_path}")

    if "kind" in case:
        valid_kinds = (
            schema.get("field_definitions", {}).get("kind", {}).get("values", [])
        )
        if case["kind"] not in valid_kinds:
            errors.append(f"Invalid kind '{case['kind']}' in {case_yaml_path}")

    if "harness" in case:
        valid_harnesses = (
            schema.get("field_definitions", {}).get("harness", {}).get("values", [])
        )
        if case["harness"] not in valid_harnesses:
            errors.append(f"Invalid harness '{case['harness']}' in {case_yaml_path}")

    # Check required files based on kind
    kind = case.get("kind")
    file_reqs = schema.get("file_requirements", {}).get(kind, {})

    for req_file in file_reqs.get("required", []):
        req_path = case_dir / req_file
        if not req_path.exists():
            errors.append(
                f"Missing required file '{req_file}' for {kind} case in {case_dir}"
            )

    return errors


def get_active_cases() -> List[Path]:
    """Get list of active case directories from execution-order.yaml."""
    exec_order = load_execution_order()
    cases_dir = Path(__file__).parent.parent

    active_cases = []
    bootstrap = exec_order.get("bootstrap_sequence", [])

    for case_id in bootstrap:
        # Find the case directory
        for phase_dir in cases_dir.iterdir():
            if phase_dir.is_dir() and phase_dir.name[0:2].isdigit():
                for case_dir in phase_dir.iterdir():
                    if case_dir.is_dir() and case_id in case_dir.name:
                        active_cases.append(case_dir)
                        break

    return active_cases


def validate_all() -> int:
    """Validate all active cases. Returns 0 on success, 1 on failure."""
    schema = load_schema()
    cases = get_active_cases()

    if not cases:
        print("No active cases found")
        return 1

    all_errors = []
    for case_dir in cases:
        errors = validate_case_yaml(case_dir, schema)
        if errors:
            all_errors.extend(errors)
            print(f"FAILED: {case_dir.name}")
            for error in errors:
                print(f"  - {error}")
        else:
            print(f"OK: {case_dir.name}")

    if all_errors:
        print(f"\n{len(all_errors)} validation errors found")
        return 1

    print("\nAll validations passed")
    return 0


def validate_one(case_dir: Path) -> int:
    """Validate a single case directory. Returns 0 on success, 1 on failure."""
    schema = load_schema()
    errors = validate_case_yaml(case_dir, schema)

    if errors:
        print(f"FAILED: {case_dir.name}")
        for error in errors:
            print(f"  - {error}")
        return 1

    print(f"OK: {case_dir.name}")
    return 0


def main():
    if len(sys.argv) < 2:
        print("Usage: case_schema.py --validate <case-dir>")
        print("       case_schema.py --validate-all")
        sys.exit(1)

    if sys.argv[1] == "--validate-all":
        sys.exit(validate_all())
    elif sys.argv[1] == "--validate":
        if len(sys.argv) < 3:
            print("Usage: case_schema.py --validate <case-dir>")
            sys.exit(1)
        case_dir = Path(sys.argv[2])
        sys.exit(validate_one(case_dir))
    else:
        print(f"Unknown command: {sys.argv[1]}")
        sys.exit(1)


if __name__ == "__main__":
    main()
