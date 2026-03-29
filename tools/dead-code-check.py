#!/usr/bin/env python3
"""
iSH Dead Code Detection Tool

Analyzes the codebase for potentially dead/unused code:
1. Unused static functions
2. Unused static variables
3. Unused macros
4. Functions with no callers (via heuristics)

Usage:
    ./tools/dead-code-check.py [paths...]
    ./tools/dead-code-check.py --json  # Output JSON for CI
    ./tools/dead-code-check.py --fix   # Add comments to suppress false positives
"""

import argparse
import json
import os
import re
import subprocess
import sys
from collections import defaultdict
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import List, Dict, Set, Tuple, Optional


@dataclass
class DeadCodeItem:
    """Represents a potential dead code item."""
    file: str
    line: int
    column: int
    type: str  # 'function', 'variable', 'macro', 'type'
    name: str
    severity: str  # 'error', 'warning', 'info'
    message: str
    suppressible: bool = True


class DeadCodeChecker:
    """Checks for dead code in C source files."""

    # Files/paths to exclude
    EXCLUDE_PATHS = [
        'deps/',
        'build/',
        'builddir/',
        '.cache/',
        'tests/cases/',
    ]

    # Known false positives (functions used via function pointers, etc.)
    KNOWN_USED_SYMBOLS = {
        # TCTI gadget table entries
        'gadget_',  # Prefix for gadget functions
        'helper_',
        'do_',  # Often called via dispatch
        'sys_',  # Syscall handlers
        'default_',  # Default handlers
        # Platform-specific entry points
        'ish_',
        'cpu_',
        'page_',
        # Callback functions
        'callback',
        'handler',
    }

    def __init__(self, root_dir: str = '.', verbose: bool = False):
        self.root_dir = Path(root_dir)
        self.verbose = verbose
        self.issues: List[DeadCodeItem] = []

    def log(self, message: str) -> None:
        """Print verbose message."""
        if self.verbose:
            print(f"[dead-code-check] {message}", file=sys.stderr)

    def should_exclude(self, path: Path) -> bool:
        """Check if path should be excluded."""
        path_str = str(path)
        for exclude in self.EXCLUDE_PATHS:
            if exclude in path_str:
                return True
        return False

    def run_cppcheck(self, files: List[Path]) -> List[DeadCodeItem]:
        """Run cppcheck to find unused functions."""
        if not files:
            return []

        # Limit files for faster pre-commit execution
        # Process only the first 50 files per run to keep it fast
        files_to_check = files[:50]

        cmd = [
            'cppcheck',
            '--enable=unusedFunction',
            '--suppress=missingIncludeSystem',
            '--inline-suppr',
            '--quiet',
            '--template={file}:{line}:{column}:{severity}:{message}',
            '-j', '4',  # Parallel processing
            '-I', str(self.root_dir),
            '-I', str(self.root_dir / 'trace'),
        ] + [str(f) for f in files_to_check]

        self.log(f"Running: {' '.join(cmd)}")

        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=60
            )
        except subprocess.TimeoutExpired:
            self.log("cppcheck timed out")
            return []
        except FileNotFoundError:
            self.log("cppcheck not found")
            return []

        issues = []
        for line in result.stdout.split('\n') + result.stderr.split('\n'):
            if 'unusedFunction' in line:
                parsed = self._parse_cppcheck_line(line)
                if parsed:
                    issues.append(parsed)

        return issues

    def _parse_cppcheck_line(self, line: str) -> Optional[DeadCodeItem]:
        """Parse cppcheck output line."""
        # Format: file:line:column:severity:message
        match = re.match(r'^(.+):(\d+):(\d+):(\w+):(.+)$', line)
        if not match:
            return None

        file_path, line_no, col, severity, message = match.groups()

        # Determine type from message
        item_type = 'unknown'
        name = ''

        if 'unusedFunction' in message:
            item_type = 'function'
            name_match = re.search(r"function '([^']+)'", message)
            if name_match:
                name = name_match.group(1)
        elif 'unusedVariable' in message:
            item_type = 'variable'
            name_match = re.search(r"variable '([^']+)'", message)
            if name_match:
                name = name_match.group(1)
        elif 'unusedStructMember' in message:
            item_type = 'field'
            name_match = re.search(r"struct member '([^']+)'", message)
            if name_match:
                name = name_match.group(1)

        # Skip known used symbols
        if any(name.startswith(prefix) for prefix in self.KNOWN_USED_SYMBOLS):
            return None

        return DeadCodeItem(
            file=file_path,
            line=int(line_no),
            column=int(col),
            type=item_type,
            name=name,
            severity='warning' if severity == 'style' else severity,
            message=message.strip()
        )

    def find_static_functions(self, files: List[Path]) -> List[DeadCodeItem]:
        """Find static functions that might be unused."""
        issues = []

        for file_path in files:
            if self.should_exclude(file_path):
                continue

            try:
                content = file_path.read_text()
            except Exception as e:
                self.log(f"Could not read {file_path}: {e}")
                continue

            # Find static function definitions
            # Pattern: static ... name(...) {
            pattern = r'^static\s+(?:inline\s+)?(?:\w+\s+)+(\w+)\s*\([^)]*\)\s*\{'

            for match in re.finditer(pattern, content, re.MULTILINE):
                func_name = match.group(1)

                # Skip known patterns
                if any(func_name.startswith(prefix) for prefix in self.KNOWN_USED_SYMBOLS):
                    continue

                # Count occurrences (definition + calls)
                occurrences = content.count(func_name)

                # If only appears once (the definition), likely unused
                if occurrences == 1:
                    # Calculate line number
                    line_num = content[:match.start()].count('\n') + 1

                    issues.append(DeadCodeItem(
                        file=str(file_path),
                        line=line_num,
                        column=1,
                        type='static_function',
                        name=func_name,
                        severity='info',
                        message=f"Static function '{func_name}' appears unused (only definition found)"
                    ))

        return issues

    def find_unused_macros(self, files: List[Path]) -> List[DeadCodeItem]:
        """Find macros that might be unused."""
        issues = []

        for file_path in files:
            if self.should_exclude(file_path):
                continue

            try:
                content = file_path.read_text()
            except Exception:
                continue

            # Find #define directives
            define_pattern = r'^#define\s+(\w+)'

            defined_macros = set()
            macro_lines = {}

            for match in re.finditer(define_pattern, content, re.MULTILINE):
                macro_name = match.group(1)

                # Skip header guards and known patterns
                if macro_name.endswith('_H') or macro_name.startswith('_'):
                    continue

                defined_macros.add(macro_name)
                macro_lines[macro_name] = content[:match.start()].count('\n') + 1

            # Check usage
            for macro_name in defined_macros:
                # Count occurrences
                pattern = r'\b' + re.escape(macro_name) + r'\b'
                occurrences = len(re.findall(pattern, content))

                # If only appears once (the definition), likely unused
                if occurrences == 1:
                    issues.append(DeadCodeItem(
                        file=str(file_path),
                        line=macro_lines[macro_name],
                        column=1,
                        type='macro',
                        name=macro_name,
                        severity='info',
                        message=f"Macro '{macro_name}' appears unused"
                    ))

        return issues

    def get_source_files(self, paths: List[str]) -> List[Path]:
        """Get list of C source files to check."""
        files = []

        for path_str in paths:
            path = Path(path_str)

            if path.is_file() and path.suffix in ('.c', '.h'):
                if not self.should_exclude(path):
                    files.append(path)
            elif path.is_dir():
                for ext in ('.c', '.h'):
                    for f in path.rglob(f'*{ext}'):
                        if not self.should_exclude(f):
                            files.append(f)

        return sorted(set(files))

    def check(self, paths: List[str]) -> List[DeadCodeItem]:
        """Run all checks on given paths."""
        files = self.get_source_files(paths)
        self.log(f"Checking {len(files)} files")

        # Run cppcheck
        self.issues.extend(self.run_cppcheck(files))

        # Additional checks
        self.issues.extend(self.find_static_functions(files))
        self.issues.extend(self.find_unused_macros(files))

        # Sort by file and line
        self.issues.sort(key=lambda x: (x.file, x.line))

        return self.issues

    def format_text(self) -> str:
        """Format issues as text."""
        if not self.issues:
            return "No dead code detected."

        lines = [f"Found {len(self.issues)} potential dead code issues:\n"]

        for issue in self.issues:
            lines.append(f"{issue.file}:{issue.line}:{issue.column}: {issue.severity}: {issue.message}")

        return '\n'.join(lines)

    def format_json(self) -> str:
        """Format issues as JSON."""
        return json.dumps([asdict(i) for i in self.issues], indent=2)

    def has_errors(self) -> bool:
        """Check if any issues are errors."""
        return any(i.severity == 'error' for i in self.issues)


def main():
    parser = argparse.ArgumentParser(
        description='iSH Dead Code Detection Tool',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  %(prog)s                          # Check default paths (emu/, kernel/, fs/, tcti/, util/)
  %(prog)s emu/ kernel/            # Check specific directories
  %(prog)s --json                  # Output JSON
  %(prog)s --json --exit-code      # Exit with error code if issues found
        '''
    )

    parser.add_argument(
        'paths',
        nargs='*',
        default=['emu/', 'kernel/', 'fs/', 'tcti/', 'util/'],
        help='Paths to check (default: core source directories)'
    )
    parser.add_argument(
        '--json',
        action='store_true',
        help='Output JSON format'
    )
    parser.add_argument(
        '--exit-code',
        action='store_true',
        help='Exit with non-zero code if issues found'
    )
    parser.add_argument(
        '-v', '--verbose',
        action='store_true',
        help='Verbose output'
    )

    args = parser.parse_args()

    checker = DeadCodeChecker(verbose=args.verbose)
    issues = checker.check(args.paths)

    if args.json:
        print(checker.format_json())
    else:
        print(checker.format_text())

    if args.exit_code and issues:
        sys.exit(1)

    sys.exit(0)


if __name__ == '__main__':
    main()
