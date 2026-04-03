#!/bin/bash
#
# iSH Performance Baseline Test Suite
#
# Run this script on each target device to collect baseline performance metrics.
# Results are exported as JSON and should be committed to results/baseline/
#
# Usage:
#   ./tests/performance/run_baseline.sh [device_name]
#
# Examples:
#   ./tests/performance/run_baseline.sh "iPhone_15_Pro_Max"
#   ./tests/performance/run_baseline.sh "iPad_9th"
#   ./tests/performance/run_baseline.sh "iPhone_SE"

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
RESULTS_DIR="$PROJECT_ROOT/results/baseline"
ISH_BINARY="$BUILD_DIR/ish"

# Device name from argument or auto-detect
if [ $# -ge 1 ]; then
    DEVICE_NAME="$1"
else
    # Try to get device model
    if command -v uname &> /dev/null; then
        DEVICE_NAME=$(uname -m)
    else
        DEVICE_NAME="unknown"
    fi
fi

# Sanitize device name for filename
DEVICE_FILE=$(echo "$DEVICE_NAME" | tr ' ' '_' | tr -cd '[:alnum:]_-')
TIMESTAMP=$(date -u +"%Y%m%d_%H%M%S")
RESULT_FILE="$RESULTS_DIR/${DEVICE_FILE}_${TIMESTAMP}.json"

echo "========================================"
echo "iSH Performance Baseline Collection"
echo "========================================"
echo "Device: $DEVICE_NAME"
echo "Results: $RESULT_FILE"
echo ""

# Create results directory
mkdir -p "$RESULTS_DIR"

# Check if binary exists
if [ ! -f "$ISH_BINARY" ]; then
    echo "Error: iSH binary not found at $ISH_BINARY"
    echo "Please build with: meson setup build -Denable_perf_stats=true && ninja -C build"
    exit 1
fi

# Collect system info
echo "=== System Information ==="
if command -v uname &> /dev/null; then
    echo "Machine: $(uname -m)"
    echo "System: $(uname -s)"
fi

if command -v sysctl &> /dev/null; then
    echo "CPU Cores: $(sysctl -n hw.ncpu 2>/dev/null || echo 'unknown')"
    MEM_BYTES=$(sysctl -n hw.memsize 2>/dev/null || echo '0')
    MEM_GB=$((MEM_BYTES / 1024 / 1024 / 1024))
    echo "Memory: ${MEM_GB}GB"
fi

echo ""

# Function to run a test and collect timing
run_test() {
    local test_name="$1"
    local command="$2"
    local iterations="${3:-1}"
    
    echo "=== Test: $test_name ==="
    
    # Warmup run
    echo "  Warming up..."
    eval "$command" > /dev/null 2>&1 || true
    
    # Timed runs
    local total_time=0
    local min_time=999999
    local max_time=0
    
    for i in $(seq 1 $iterations); do
        local start_time=$(date +%s%N 2>/dev/null || echo $(($(date +%s) * 1000000000)))
        eval "$command" > /dev/null 2>&1 || true
        local end_time=$(date +%s%N 2>/dev/null || echo $(($(date +%s) * 1000000000)))
        
        local elapsed=$((end_time - start_time))
        total_time=$((total_time + elapsed))
        
        if [ $elapsed -lt $min_time ]; then
            min_time=$elapsed
        fi
        if [ $elapsed -gt $max_time ]; then
            max_time=$elapsed
        fi
        
        echo "  Run $i: $(echo "scale=3; $elapsed / 1000000000" | bc 2>/dev/null || echo "N/A")s"
    done
    
    local avg_time=$(echo "scale=3; $total_time / $iterations / 1000000000" | bc 2>/dev/null || echo "N/A")
    local min_sec=$(echo "scale=3; $min_time / 1000000000" | bc 2>/dev/null || echo "N/A")
    local max_sec=$(echo "scale=3; $max_time / 1000000000" | bc 2>/dev/null || echo "N/A")
    
    echo "  Average: ${avg_time}s"
    echo "  Min: ${min_sec}s"
    echo "  Max: ${max_sec}s"
    echo ""
    
    # Return average time in nanoseconds
    echo $((total_time / iterations))
}

# Initialize JSON
JSON_FILE="$RESULT_FILE"
echo "{" > "$JSON_FILE"
echo "  \"device\": \"$DEVICE_NAME\"," >> "$JSON_FILE"
echo "  \"timestamp\": \"$(date -u +"%Y-%m-%dT%H:%M:%SZ")\"," >> "$JSON_FILE"
echo "  \"git_commit\": \"$(cd $PROJECT_ROOT && git rev-parse --short HEAD 2>/dev/null || echo 'unknown')\"," >> "$JSON_FILE"

# System info
echo "  \"system\": {" >> "$JSON_FILE"
if command -v uname &> /dev/null; then
    echo "    \"machine\": \"$(uname -m)\"," >> "$JSON_FILE"
    echo "    \"system\": \"$(uname -s)\"" >> "$JSON_FILE"
fi
echo "  }," >> "$JSON_FILE"

# CPU info
if command -v sysctl &> /dev/null; then
    echo "  \"cpu\": {" >> "$JSON_FILE"
    echo "    \"cores\": $(sysctl -n hw.ncpu 2>/dev/null || echo '0')," >> "$JSON_FILE"
    echo "    \"memory_bytes\": $(sysctl -n hw.memsize 2>/dev/null || echo '0')" >> "$JSON_FILE"
    echo "  }," >> "$JSON_FILE"
fi

# Begin tests section
echo "  \"tests\": {" >> "$JSON_FILE"

FIRST_TEST=true

# Test 1: Syscall getpid (microbenchmark)
# echo "Test 1: Syscall getpid (1M iterations)"
# This would require a C test binary, skipping for now

# Test 2: Shell startup time
if [ -f "$ISH_BINARY" ]; then
    if [ "$FIRST_TEST" = true ]; then
        FIRST_TEST=false
    else
        echo "," >> "$JSON_FILE"
    fi
    
    echo "Running: Shell startup test"
    TIME_NS=$(run_test "shell_startup" "$ISH_BINARY /bin/sh -c 'exit 0'" 3)
    TIME_SEC=$(echo "scale=6; $TIME_NS / 1000000000" | bc 2>/dev/null || echo "null")
    echo "    \"shell_startup_ns\": $TIME_NS," >> "$JSON_FILE"
    echo "    \"shell_startup_sec\": $TIME_SEC" >> "$JSON_FILE"
fi

# Test 3: Simple loop (TB cache test)
if [ -f "$ISH_BINARY" ]; then
    echo "," >> "$JSON_FILE"
    echo "Running: Loop test (10k iterations)"
    TIME_NS=$(run_test "loop_10k" "$ISH_BINARY /bin/sh -c 'for i in \$(seq 1 10000); do :; done'" 3)
    TIME_SEC=$(echo "scale=6; $TIME_NS / 1000000000" | bc 2>/dev/null || echo "null")
    echo "    \"loop_10k_ns\": $TIME_NS," >> "$JSON_FILE"
    echo "    \"loop_10k_sec\": $TIME_SEC" >> "$JSON_FILE"
fi

# End JSON
echo "  }" >> "$JSON_FILE"
echo "}" >> "$JSON_FILE"

echo "========================================"
echo "Baseline collection complete!"
echo "Results saved to: $RESULT_FILE"
echo ""
echo "To commit this baseline:"
echo "  git add $RESULT_FILE"
echo "  git commit -m \"Add baseline for $DEVICE_NAME\""
echo "========================================"
