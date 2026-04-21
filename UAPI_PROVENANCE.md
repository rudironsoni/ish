# Linux UAPI Headers Provenance

## Source
- **Linux Kernel Version**: 6.12
- **Source URL**: https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.12.tar.xz
- **Architecture**: arm64

## Generation Command
```bash
cd /tmp/linux-6.12
PATH="/opt/homebrew/opt/gnu-sed/libexec/gnubin:$PATH" \
  gmake ARCH=arm64 headers_install INSTALL_HDR_PATH=/tmp/linux-6.12-headers
```

## Installation
```bash
rm -rf third_party/linux-uapi/6.12/arm64/include
cp -r /tmp/linux-6.12-headers/include third_party/linux-uapi/6.12/arm64/include
```

## Verification
- All headers contain `SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note`
- Generated: 2026-04-21
- Total headers: 592 files
- Verified with: `diff -r /tmp/linux-6.12-headers/include/ third_party/linux-uapi/6.12/arm64/include/`

## References
- Linux kernel documentation: https://www.kernel.org/doc/html/next/kbuild/headers_install.html
- XcodeGen project spec: https://yonaskolb.github.io/XcodeGen/Docs/ProjectSpec.html
