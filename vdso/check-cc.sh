#!/bin/sh
set -eu

cc=$1
ld_lld=$2
test_c=$(mktemp)
trap 'rm -f "$test_c"' EXIT

cat > "$test_c" <<'END'
#if !defined(__aarch64__) && !defined(__ELF__)
#error "__aarch64__ or __ELF__ is not defined"
#endif
END

printf '$ %s\n' "$cc -target aarch64-linux-gnu -fuse-ld=$ld_lld -shared -nostdlib -x c $test_c -o /dev/null"
"$cc" -target aarch64-linux-gnu "-fuse-ld=$ld_lld" -shared -nostdlib -x c "$test_c" -o /dev/null
