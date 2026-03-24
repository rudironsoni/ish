#!/bin/sh
cc=$1
test_c=$(mktemp)
cat > $test_c <<END
#if !defined(__aarch64__) && !defined(__ELF__)
#error "__aarch64__ or __ELF__ is not defined"
#endif
END
cmd="$cc -target aarch64-linux-gnu -fuse-ld=lld -shared -nostdlib -x c $test_c -o /dev/null"
echo $ $cmd
$cmd
status=$?
exit $status
