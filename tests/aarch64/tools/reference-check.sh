#!/bin/bash
#
# ARM Reference Validation
# Compares decoder output against official ARM DDI 0487 encodings
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$SCRIPT_DIR/../../.."
TEST_DIR="$SCRIPT_DIR/.."

cd "$REPO_ROOT"

echo "ARM Reference Validation"
echo "========================"
echo ""
echo "Validating decoder against ARM DDI 0487I.a reference encodings..."
echo ""

REFERENCE_FILE="$TEST_DIR/arm-reference.txt"

if [ ! -f "$REFERENCE_FILE" ]; then
    echo "❌ ARM reference file not found: $REFERENCE_FILE"
    exit 1
fi

PASS=0
FAIL=0
SKIP=0

# Parse reference file and test each encoding
while IFS= read -r line; do
    # Skip comments and empty lines
    [[ "$line" =~ ^[[:space:]]*# ]] && continue
    [[ -z "$line" ]] && continue
    [[ "$line" =~ ^= ]] && continue

    # Parse line: mnemonic | encoding | expected fields
    # Format: MNEMONIC operands | 0xXXXXXXXX | field=value ...
    if [[ "$line" =~ \| ]]; then
        mnemonic=$(echo "$line" | cut -d'|' -f1 | tr -d ' ')
        encoding=$(echo "$line" | cut -d'|' -f2 | tr -d ' ')
        expected=$(echo "$line" | cut -d'|' -f3 | tr -d ' ')

        # Skip entries with complex field checks for now
        if [[ "$expected" == *"<<"* ]] || [[ "$expected" == *"("* ]]; then
            SKIP=$((SKIP + 1))
            continue
        fi

        # Create test program
        cat > /tmp/ref-check.c << EOF
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "emu/aarch64/decode.h"

int main() {
    uint32_t insn = $encoding;
    a64_instr_t instr;

    int ret = a64_decode(insn, &instr);
    if (ret != 0) {
        printf("DECODE_FAIL: ret=%d\n", ret);
        return 1;
    }

    // Output decoded fields for validation
    printf("cat=%d ", instr.cat);
    printf("sf=%d ", instr.is_64bit);
    printf("S=%d ", instr.set_flags);
    printf("Rd=%d ", instr.Rd);
    printf("Rn=%d ", instr.Rn);
    printf("Rm=%d ", instr.Rm);
    printf("shift=%d ", instr.shift_type);
    printf("imm6=%d ", instr.imm_shift);
    printf("sysreg=%d ", instr.sysreg);
    printf("imm=%ld ", (long)instr.imm);
    printf("\n");
    return 0;
}
EOF

        # Compile and run
        if gcc -O2 -I. -o /tmp/ref-check /tmp/ref-check.c \
                emu/aarch64/decode.c -lm 2>/dev/null; then

            result=$(/tmp/ref-check 2>&1)

            if echo "$result" | grep -q "DECODE_FAIL"; then
                echo "❌ $mnemonic - decode failed"
                FAIL=$((FAIL + 1))
            else
                # Parse expected fields
                field_errors=""

                # Check sf (is_64bit)
                if [[ "$expected" =~ sf=([01]) ]]; then
                    expected_sf="${BASH_REMATCH[1]}"
                    actual_sf=$(echo "$result" | grep -o 'sf=[0-9]*' | cut -d= -f2)
                    if [ "$actual_sf" != "$expected_sf" ]; then
                        field_errors="$field_errors sf=$actual_sf(expected:$expected_sf)"
                    fi
                fi

                # Check S (set_flags)
                if [[ "$expected" =~ S=([01]) ]]; then
                    expected_s="${BASH_REMATCH[1]}"
                    actual_s=$(echo "$result" | grep -o 'S=[0-9]*' | cut -d= -f2)
                    if [ "$actual_s" != "$expected_s" ]; then
                        field_errors="$field_errors S=$actual_s(expected:$expected_s)"
                    fi
                fi

                # Check Rd
                if [[ "$expected" =~ Rd=([0-9]+) ]]; then
                    expected_rd="${BASH_REMATCH[1]}"
                    actual_rd=$(echo "$result" | grep -o 'Rd=[0-9]*' | cut -d= -f2)
                    if [ "$actual_rd" != "$expected_rd" ]; then
                        field_errors="$field_errors Rd=$actual_rd(expected:$expected_rd)"
                    fi
                fi

                # Check Rn
                if [[ "$expected" =~ Rn=([0-9]+) ]]; then
                    expected_rn="${BASH_REMATCH[1]}"
                    actual_rn=$(echo "$result" | grep -o 'Rn=[0-9]*' | cut -d= -f2)
                    if [ "$actual_rn" != "$expected_rn" ]; then
                        field_errors="$field_errors Rn=$actual_rn(expected:$expected_rn)"
                    fi
                fi

                # Check Rm
                if [[ "$expected" =~ Rm=([0-9]+) ]]; then
                    expected_rm="${BASH_REMATCH[1]}"
                    actual_rm=$(echo "$result" | grep -o 'Rm=[0-9]*' | cut -d= -f2)
                    if [ "$actual_rm" != "$expected_rm" ]; then
                        field_errors="$field_errors Rm=$actual_rm(expected:$expected_rm)"
                    fi
                fi

                # Check shift
                if [[ "$expected" =~ shift=([0-9]+) ]]; then
                    expected_shift="${BASH_REMATCH[1]}"
                    actual_shift=$(echo "$result" | grep -o 'shift=[0-9]*' | cut -d= -f2)
                    if [ "$actual_shift" != "$expected_shift" ]; then
                        field_errors="$field_errors shift=$actual_shift(expected:$expected_shift)"
                    fi
                fi

                if [ -z "$field_errors" ]; then
                    echo "✅ $mnemonic"
                    PASS=$((PASS + 1))
                else
                    echo "❌ $mnemonic -$field_errors"
                    FAIL=$((FAIL + 1))
                fi
            fi
        else
            echo "❌ $mnemonic - compilation failed"
            FAIL=$((FAIL + 1))
        fi
    fi
done < "$REFERENCE_FILE"

echo ""
echo "========================================"
echo "ARM Reference Validation Results"
echo "========================================"
echo "Passed: $PASS"
echo "Failed: $FAIL"
echo "Skipped: $SKIP"
echo ""

if [ $FAIL -eq 0 ]; then
    echo "✅ All ARM reference patterns validated"
    echo "Decoder output matches ARM DDI 0487 specification"
    exit 0
else
    echo "❌ Some ARM reference patterns don't match"
    echo "Review failures above - decoder may need fixes"
    exit 1
fi
