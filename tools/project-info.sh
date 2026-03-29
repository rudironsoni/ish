#!/bin/bash
# Show project info

echo "iSH - Linux shell emulator for iOS"
echo ""
echo "Build system: Meson + Ninja"
echo "Architecture: AArch64 (TCTI execution engine)"
echo ""
echo "Key directories:"
echo "  emu/      - CPU emulator (AArch64 decode, TLB, memory)"
echo "  kernel/   - Syscall implementation"
echo "  fs/       - Filesystem layer"
echo "  tcti/     - Threaded Code Translation and Interpretation"
echo "  tests/    - Unit and integration tests"
