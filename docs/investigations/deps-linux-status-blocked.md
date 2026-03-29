# Track B: Linux Submodule Modernization Status

**Status:** BLOCKED  
**Blocker:** Linux submodule not properly initialized/analyzable  
**Date:** 2026-03-29  
**Branch:** feat/aarch64-migration

---

## Executive Summary

Track B (strategic `deps/linux` modernization) is **BLOCKED** because the Linux submodule at `deps/linux` is not properly populated with Linux kernel source code. The submodule pointer references commit `33ba00328916597d1d1b9096be892651eab06844`, but this commit cannot be found in the expected Linux fork repository.

**Impact:** Full commit-level fork analysis, patch-queue planning, and upstream migration assessment **cannot proceed** until this blocker is resolved.

---

## Blocker Details

### Submodule Configuration (from .gitmodules)

```ini
[submodule "deps/linux"]
    path = deps/linux
    url = https://github.com/rudironsoni/linux
    update = none
    shallow = true
```

### Current Submodule State

```bash
$ git submodule status deps/linux
33ba00328916597d1d1b9096be892651eab06844 deps/linux (heads/ish)
```

**Expected:** Linux kernel source tree with `arch/ish/` port  
**Actual:** iSH repository content (same as main repo)

### Evidence of Broken State

```bash
$ ls deps/linux/arch/
(No output - directory doesn't exist)

$ cat deps/linux/Makefile | head -5
# iSH Build Makefile
# Pre-build setup and iOS build automation

$ git -C deps/linux log --oneline -1
21074752 Add profiling infrastructure and dead code detection, migrate from justfile to meson
# ^ This is an iSH repo commit, NOT a Linux kernel commit

$ git -C deps/linux remote -v
origin  git@github.com:rudironsoni/ish.git (fetch)
origin  git@github.com:rudironsoni/ish.git (push)
# ^ Points to iSH repo, not Linux repo
```

### Attempted Recovery Steps

1. **Standard submodule update:**
   ```bash
   git submodule update --init --recursive deps/linux
   # Result: Silent success, but content is iSH repo, not Linux
   ```

2. **Force reinitialization:**
   ```bash
   git submodule deinit -f deps/linux
   rm -rf .git/modules/deps/linux
   git submodule update --init deps/linux
   # Result: "fatal: could not get a repository handle for submodule 'deps/linux'"
   ```

3. **Clone fork directly:**
   ```bash
   git clone https://github.com/rudironsoni/linux.git deps/linux-temp
   cd deps/linux-temp
   git checkout 33ba00328916597d1d1b9096be892651eab06844
   # Result: "fatal: unable to read tree (33ba00328916597d1d1b9096be892651eab06844)"
   # Commit does not exist in the fork
   ```

### Root Cause Analysis

The commit `33ba00328916597d1d1b9096be892651eab06844` referenced in the submodule:
1. **Does not exist** in `https://github.com/rudironsoni/linux`
2. **Does exist** in the main `rudironsoni/ish` repository
3. **Is not** a Linux kernel commit

**Possible explanations:**
- The submodule pointer was accidentally set to an iSH repo commit
- The Linux fork was rebased/rewritten and the old commit is orphaned
- The submodule was never properly initialized with Linux content
- The fork URL in .gitmodules is incorrect

---

## What Was Documented (From Build Files Only)

Despite the blocker, the **dependency surface** was analyzed from build system files:

### Build-Time Dependencies (from deps/meson.build)

When `kernel=linux` option is selected:

| Component | Type | Required |
|-----------|------|----------|
| `deps/linux-build.sh` | Build script | YES |
| `deps/kconfig-fragment.sh` | Build script | YES |
| `deps/makefilter.py` | Build script | YES |
| `deps/linux.config` | Config fragment | YES |
| `arch/ish/configs/ish_defconfig` | Architecture config | YES |
| `ARCH=ish` | Build flag | YES |
| `liblinux.a` | Output artifact | YES |

### Source-Level Dependencies

Files compiled ONLY when `kernel=linux`:

| File | Dependencies |
|------|--------------|
| `linux/main.c` | Linux kernel headers, entry point |
| `linux/fakefs.c` | Linux VFS, fakefs implementation |
| `app/LinuxInterop.c` | Linux kernel APIs |
| `app/LinuxRoot.c` | Linux mount/init APIs |
| `app/LinuxTTY.c` | Linux TTY layer |
| `app/LinuxPTY.c` | Linux PTY devices |
| `app/PasteboardDeviceLinux.c` | Linux miscdevice |

### Header Dependencies

From `deps/meson.build`, the following include paths are required:

```
deps/linux/arch/ish/include
deps/linux/arch/ish/include/generated
deps/linux/include
deps/linux/arch/ish/include/uapi
deps/linux/arch/ish/include/generated/uapi
deps/linux/include/uapi
deps/linux/include/generated/uapi
```

### Architectural Dependencies

| Component | Description | Status |
|-----------|-------------|--------|
| `arch/ish/` | Custom Linux architecture port | **UNKNOWN** - cannot analyze |
| `arch/ish/kernel/vmlinux.lds.S` | Linker script | **UNKNOWN** |
| `arch/ish/include/user/*.h` | Host integration headers | **UNKNOWN** |
| `CONFIG_ISH_LINK_OBJECT` | Build configuration | **UNKNOWN** |

---

## What Remains Unknown

Until the submodule is fixed, the following **cannot be determined**:

1. **Fork delta:** How many commits differ from upstream Linux
2. **Commit classifications:** Which changes are DROP/REPLACE/MOVE/PATCH
3. **Upstream version:** What Linux version is the fork based on
4. **Patch queue size:** How many patches would be required
5. **arch/ish scope:** What custom architecture code exists
6. **x86 legacy:** What x86-specific code remains
7. **Feasibility:** Whether zero-patch or minimal-patch is achievable

---

## Unblocking Requirements

To unblock Track B, **ONE** of the following must be true:

### Option 1: Fix Submodule Pointer
- Correct the submodule to point to a valid Linux kernel commit
- Ensure `deps/linux` contains actual Linux kernel source
- Verify `arch/ish/` directory exists

### Option 2: Update Fork Reference
- Update `.gitmodules` to point to correct Linux fork URL
- The fork must contain the `ish` branch with proper Linux+arch/ish content
- Commit history must be available for analysis

### Option 3: Document Expected State
- Provide explicit documentation of what the Linux submodule should contain
- Specify the expected upstream Linux version
- Provide access to the actual fork repository

---

## Proven Dependency Surface (Build Analysis Only)

From analyzing build files without functional submodule:

```yaml
deps_linux_dependency_map:
  build_system:
    - path: deps/meson.build
      kind: conditional_build
      dependency: "if get_option('kernel') == 'linux'"
      required_for: kernel=linux builds only
    - path: deps/linux-build.sh
      kind: build_script
      dependency: "ARCH=ish, ish_defconfig, liblinux.a"
      required_for: kernel=linux builds only
    - path: deps/linux.config
      kind: config_fragment
      dependency: "Kconfig fragments"
      required_for: kernel=linux builds only
      
  source_includes:
    - path: linux/main.c
      includes: "<linux/start_kernel.h>, <linux/slab.h>"
      why: "Entry point for kernel=linux mode"
    - path: linux/fakefs.c
      includes: "<linux/fs.h>, <linux/dcache.h>"
      why: "fakefs as kernel module"
    - path: app/Linux*.c (7 files)
      includes: "Linux kernel headers"
      why: "iOS/Linux interop layer"
      
  architecture_assumptions:
    - component: arch/ish/
      assumption: "ARCH=ish architecture port exists"
      upstream_blocker: yes
    - component: arch/ish/include/user/*.h
      assumption: "Host integration headers"
      upstream_blocker: yes
    - component: vmlinux.lds.S
      assumption: "Custom linker script for liblinux.a"
      upstream_blocker: yes
      
  runtime_surfaces:
    - component: run_kernel()
      impact: "Entry point from linux/main.c"
    - component: fakefs_mount
      impact: "Root filesystem mounting"
    - component: Host networking (golp)
      impact: "Networking via host"
```

---

## Track B Status Summary

| Aspect | Status |
|--------|--------|
| Submodule initialized | NO |
| Linux source available | NO |
| Fork commit accessible | NO |
| Build dependency analysis | PARTIAL (from build files only) |
| Commit-level analysis | BLOCKED |
| Patch queue planning | BLOCKED |
| Upstream migration assessment | BLOCKED |

---

## Recommendation

**Priority:** Resolve Track A (app crash) first.  
**Track B Status:** Documented as BLOCKED with known blocker.  
**Unblocking Action:** Requires manual intervention to fix submodule pointer or fork reference.

**Do not proceed with Track B analysis until:**
1. `deps/linux` contains actual Linux kernel source code, OR
2. The expected fork URL and commit are documented, OR
3. An alternative analysis method is provided
