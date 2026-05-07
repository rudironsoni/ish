# [iSH](https://ish.app)

[![Build Status](https://github.com/ish-app/ish/actions/workflows/ci.yml/badge.svg)](https://github.com/ish-app/ish/actions)
[![goto counter](https://img.shields.io/github/search/ish-app/ish/goto.svg)](https://github.com/ish-app/ish/search?q=goto)
[![fuck counter](https://img.shields.io/github/search/ish-app/ish/fuck.svg)](https://github.com/ish-app/ish/search?q=fuck)
[![shit counter](https://img.shields.io/github/search/ish-app/ish/shit.svg)](https://github.com/ish-app/ish/search?q=shit)

<p align="center">
<a href="https://ish.app">
<img src="https://ish.app/assets/github-readme.png">
</a>
</p>

A project to get a Linux shell running on iOS, using AArch64 usermode emulation and syscall translation.

For the current status of the project, check the issues tab, and the commit logs.

- [App Store page](https://apps.apple.com/us/app/ish-shell/id1436902243)
- [TestFlight beta](https://testflight.apple.com/join/97i7KM8O)
- [Discord server](https://discord.gg/HFAXj44)
- [Wiki with help and tutorials](https://github.com/ish-app/ish/wiki)
- [README中文](https://github.com/ish-app/ish/blob/master/README_ZH.md) (如若未能保持最新，请提交PR以更新)

# Hacking

This project has a git submodule, make sure to clone with `--recurse-submodules` or run `git submodule update --init` after cloning.

You'll need these things to build the project:

 - Python 3
   + Meson (`pip3 install meson`)
 - Ninja
 - Clang and LLD (on mac, `brew install llvm`, on linux, `sudo apt install clang lld` or `sudo pacman -S clang lld` or whatever)
 - sqlite3 (this is so common it may already be installed on linux and is definitely already installed on mac. if not, do something like `sudo apt install libsqlite3-dev`)
 - libarchive (`brew install libarchive`, `sudo port install libarchive`, `sudo apt install libarchive-dev`) TODO: bundle this dependency

## Build for iOS

Open the project in Xcode, open iSH.xcconfig, and change `ROOT_BUNDLE_IDENTIFIER` to something unique. You'll also need to update the development team ID in the project (not target!) build settings. Then click Run. There are scripts that should do everything else automatically. If you run into any problems, open an issue and I'll try to help.

## Build command line tool for testing

### Using Just (recommended)

Install [just](https://github.com/casey/just) (`brew install just`), then run:

```bash
just build    # Build the project
just test     # Run tests
just lint     # Run linting
```

See `just --list` for all available commands.

### Using Meson directly

To set up your environment, cd to the project and run `meson build` to create a build directory in `build`. Then cd to the build directory and run `ninja`.

To set up a self-contained Alpine linux filesystem, download the Alpine minirootfs tarball for aarch64 from the [Alpine website](https://alpinelinux.org/downloads/), extract it into a directory such as `alpine`, and then run `./ish -r alpine /bin/sh`.



## Logging

iSH has several logging channels which can be enabled at build time. By default, all of them are disabled. To enable them:

- In Xcode: Set the `ISH_LOG` setting in iSH.xcconfig to a space-separated list of log channels.
- With Meson (command line tool for testing): Run `meson configure -Dlog="<space-separated list of log channels>"`.

Available channels:

- `strace`: The most useful channel, logs the parameters and return value of almost every system call.
- `instr`: Logs every instruction executed by the emulator. This slows things down a lot.
- `verbose`: Debug logs that don't fit into another category.
- Grep for `DEFAULT_CHANNEL` to see if more log channels have been added since this list was updated.

## Profiling

iSH includes profiling infrastructure for performance analysis. This helps identify bottlenecks in the TCTI emulator, track translation block compilation, and analyze TLB performance.

### Building with Profiling

```bash
meson setup builddir -Denable_profiling=true
ninja -C builddir
```

### Running with Profiling

```bash
# Run and capture profile
ISH_PROFILE_OUTPUT=profile.json ./builddir/ish -f alpine /bin/sh

# Or use the provided script
./scripts/profile-run.sh -f alpine /bin/sh
```

### Analyzing Profiles

```bash
# Analyze profile output
python3 tools/ish-profile-tool.py analyze profile.json

# Generate flame graph data
python3 tools/ish-profile-tool.py flamegraph profile.json > flame.txt

# Compare two profiles
python3 tools/ish-profile-tool.py compare baseline.json current.json
```

### Profile Events

The profiler captures events including:
- Translation block compilation and execution
- TLB misses and cache performance
- Memory allocations
- System calls

See `docs/profiling.md` for detailed documentation.

# A note on the TCTI execution engine

iSH uses a Threaded Code Translation and Interpretation (TCTI) engine for AArch64 guest emulation. The engine generates an array of pointers to functions called gadgets, and each gadget ends with a tailcall to the next function; like the threaded code technique used by some Forth interpreters. This branch is AArch64 guest only - there is no x86 or i386 support.
