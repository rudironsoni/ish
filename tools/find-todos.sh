#!/bin/bash
# Find and track TODO/FIXME/XXX comments in source
# Usage: find-todos.sh [options]
#   --json          Output in JSON format
#   --exit-code     Exit with error if TODOs found without issue references
#   --enforce-refs  Require TODOs to reference issues (e.g., TODO(#123))

set -e

# Directories to scan
DIRS="emu kernel fs tcti util"

# Patterns to search for
PATTERNS="TODO|FIXME|XXX|HACK|BUG"

# Output format
JSON_OUTPUT=false
EXIT_CODE=false
ENFORCE_REFS=false

# Parse arguments
for arg in "$@"; do
    case $arg in
        --json)
            JSON_OUTPUT=true
            shift
            ;;
        --exit-code)
            EXIT_CODE=true
            shift
            ;;
        --enforce-refs)
            ENFORCE_REFS=true
            shift
            ;;
    esac
done

# Find all TODO/FIXME comments
declare -a TODOS=()
declare -a TODO_COUNT=0

for dir in $DIRS; do
    if [ -d "$dir" ]; then
        while IFS= read -r line; do
            TODOS+=("$line")
            ((TODO_COUNT++))
        done < <(grep -rn "\($PATTERNS\)" --include="*.c" --include="*.h" "$dir" 2>/dev/null | grep -v "^Binary" || true)
    fi
done

# Output results
if [ "$JSON_OUTPUT" = true ]; then
    echo "{"
    echo "  \"total\": $TODO_COUNT,"
    echo "  \"items\": ["
    first=true
    for todo in "${TODOS[@]}"; do
        if [ "$first" = true ]; then
            first=false
        else
            echo ","
        fi
        # Escape for JSON
        file=$(echo "$todo" | cut -d: -f1)
        line=$(echo "$todo" | cut -d: -f2)
        text=$(echo "$todo" | cut -d: -f3- | sed 's/"/\\"/g')
        echo -n "    {\"file\": \"$file\", \"line\": $line, \"text\": \"$text\"}"
    done
    echo ""
    echo "  ]"
    echo "}"
else
    echo "Found $TODO_COUNT TODO/FIXME/XXX comments:"
    echo ""
    for todo in "${TODOS[@]}"; do
        echo "  $todo"
    done
fi

# Check for TODOs without issue references
if [ "$ENFORCE_REFS" = true ]; then
    UNREFERENCED=0
    for todo in "${TODOS[@]}"; do
        # Check if TODO has issue reference pattern (e.g., TODO(#123) or TODO(issue-123))
        if ! echo "$todo" | grep -qE '(TODO|FIXME|XXX|HACK|BUG)[\s:]*[\(\[#][0-9]+'; then
            ((UNREFERENCED++))
            if [ "$JSON_OUTPUT" = false ]; then
                echo "WARNING: Unreferenced TODO: $todo"
            fi
        fi
    done

    if [ $UNREFERENCED -gt 0 ]; then
        echo ""
        echo "Found $UNREFERENCED TODO(s) without issue references."
        echo "Please use format: TODO(#123) or TODO(issue-123)"
        if [ "$EXIT_CODE" = true ]; then
            exit 1
        fi
    fi
fi

exit 0
