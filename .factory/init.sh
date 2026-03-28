#!/bin/bash
# Factory init script for iOS App Testing Harness
# This script is idempotent and safe to run multiple times

set -e

echo "=== iOS App Testing Harness Initialization ==="

# Verify XcodeBuildMCP is available
if ! command -v xcodebuildmcp &> /dev/null; then
    echo "Warning: XcodeBuildMCP command not found in PATH"
    echo "XcodeBuildMCP tools should be available via MCP server"
fi

# Verify iSH.xcodeproj exists
if [ ! -d "iSH.xcodeproj" ]; then
    echo "Error: iSH.xcodeproj not found in repository root"
    exit 1
fi

echo "✓ iSH.xcodeproj found"

# Verify case directories exist
for phase in 02b-ios-simulator-harness 02c-ios-app-runtime-entry; do
    if [ -d "tests/cases/$phase" ]; then
        echo "✓ Phase directory exists: $phase"
    else
        echo "Warning: Phase directory missing: $phase"
    fi
done

# Check for existing XcodeBuildMCP configuration
echo ""
echo "XcodeBuildMCP configuration should be pre-configured:"
echo "  - projectPath: iSH.xcodeproj"
echo "  - scheme: iSH"
echo "  - simulatorName: iPhone 17 Pro"
echo ""

# Verify write access to case directories
if [ -w "tests/cases/02b-ios-simulator-harness" ]; then
    echo "✓ Write access to 02b case directory"
else
    echo "Error: No write access to case directories"
    exit 1
fi

echo ""
echo "=== Initialization Complete ==="
echo "Ready to execute app testing harness cases"
