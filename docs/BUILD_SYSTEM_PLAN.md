# iSH Build System Plan: macOS + Linux (osxcross)

## Overview

Dual-platform build system supporting:
1. **macOS Native**: Full iOS development with Xcode
2. **Linux + osxcross**: CI/CD frugality, cross-compilation in Docker

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        iSH Build System                                  │
├─────────────────────────────────┬───────────────────────────────────────┤
│      macOS (Primary)            │      Linux + Docker (CI/CD)          │
│                                 │                                        │
│  ┌─────────────────────────┐   │   ┌────────────────────────────────┐  │
│  │  Xcode + iOS SDK        │   │   │  osxcross toolchain            │  │
│  │  - Native compilation   │   │   │  - Linux-hosted                │  │
│  │  - iOS Simulator        │   │   │  - Targets: macOS/iOS ARM64    │  │
│  │  - Device testing       │   │   │  - No Simulator (build only)   │  │
│  └───────────┬─────────────┘   │   └──────────────┬─────────────────┘  │
│              │                  │                  │                    │
│  ┌───────────▼─────────────┐   │   ┌──────────────▼─────────────────┐  │
│  │  Build Targets          │   │   │  Build Targets                  │  │
│  │  - iSH.app (device)     │   │   │  - ish-cli (macOS binary)       │  │
│  │  - iSH.app (simulator)  │   │   │  - Unit test binaries           │  │
│  │  - XCTest bundles       │   │   │  - Static analysis              │  │
│  └───────────┬─────────────┘   │   └──────────────┬─────────────────┘  │
│              │                  │                  │                    │
│  ┌───────────▼─────────────┐   │   ┌──────────────▼─────────────────┐  │
│  │  Testing                │   │   │  Testing                        │  │
│  │  - Unit tests (XCTest)  │   │   │  - Decoder unit tests           │  │
│  │  - UI tests (XCUITest)  │   │   │  - ARM reference validation     │  │
│  │  - Integration tests    │   │   │  - QEMU comparison (E2E)        │  │
│  │  - Performance tests    │   │   │  - Valgrind/ASan checks         │  │
│  └─────────────────────────┘   │   └─────────────────────────────────┘  │
└─────────────────────────────────┴───────────────────────────────────────┘
```

## 1. macOS Native Build (Primary Development)

### Prerequisites
```bash
# macOS 14+ with Xcode 15+
xcode-select --install

# Verify installation
xcodebuild -version
xcrun --show-sdk-path --sdk iphoneos
```

### Build Targets

```bash
# iOS Device (arm64)
xcodebuild \
  -project iSH.xcodeproj \
  -scheme iSH \
  -destination 'generic/platform=iOS' \
  -configuration Release \
  build

# iOS Simulator (arm64/x86_64)
xcodebuild \
  -project iSH.xcodeproj \
  -scheme iSH \
  -destination 'platform=iOS Simulator,name=iPhone 15' \
  -configuration Debug \
  build

# macOS (for CLI testing)
xcodebuild \
  -project iSH.xcodeproj \
  -scheme iSH \
  -destination 'platform=macOS' \
  -configuration Debug \
  build
```

### Testing on macOS

```bash
# Unit tests
xcodebuild test \
  -project iSH.xcodeproj \
  -scheme "iSH Aarch64 Tests" \
  -destination 'platform=iOS Simulator,name=iPhone 15'

# UI tests
xcodebuild test \
  -project iSH.xcodeproj \
  -scheme "iSH Aarch64 UI Tests" \
  -destination 'platform=iOS Simulator,name=iPhone 15'

# Full test suite
xcodebuild test \
  -project iSH.xcodeproj \
  -scheme iSH \
  -destination 'platform=iOS Simulator,name=iPhone 15' \
  | xcpretty
```

## 2. Linux + osxcross Build (CI/CD)

### osxcross Setup

```dockerfile
# Dockerfile.osxcross
FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    clang \
    llvm \
    lld \
    cmake \
    ninja-build \
    libssl-dev \
    libxml2-dev \
    libz-dev \
    libbz2-dev \
    cctools-port \
    git \
    wget \
    curl \
    python3 \
    python3-pip

# Download osxcross
RUN git clone https://github.com/tpoechtrager/osxcross.git /opt/osxcross
WORKDIR /opt/osxcross

# Download macOS SDK (developer must provide or use extraction tools)
# Note: SDK must be obtained legally from Xcode
COPY MacOSX14.0.sdk.tar.xz /opt/osxcross/tarballs/

# Build osxcross
RUN UNATTENDED=1 OSX_VERSION_MIN=14.0 ./build.sh

# Set environment
ENV PATH="/opt/osxcross/target/bin:$PATH"
ENV CC="o64-clang"
ENV CXX="o64-clang++"
```

### Build Targets (Linux)

```bash
# Build CLI version for macOS (no UI)
osxcross/bin/aarch64-apple-darwin23-clang \
  -o ish-cli \
  -I. \
  -Iemu \
  -Ikernel \
  -framework Foundation \
  -framework CoreFoundation \
  main.c \
  emu/aarch64/*.c \
  kernel/aarch64/*.c \
  -DCLI_ONLY=1

# Build decoder test binary
osxcross/bin/aarch64-apple-darwin23-clang \
  -o test-decoder \
  -I. \
  tests/aarch64/decoder_test.c \
  emu/aarch64/decode.c
```

### Testing on Linux

```bash
# ARM reference validation (no macOS needed)
bash tests/aarch64/tools/reference-check.sh

# QEMU-based E2E tests
bash tests/aarch64/e2e/run-e2e-tests.sh

# Static analysis
clang-tidy \
  emu/aarch64/*.c \
  -- \
  -I. -Iemu -Ikernel

# Memory safety (if QEMU tests run)
valgrind --leak-check=full ./test-decoder
```

## 3. Docker Compose Setup

```yaml
# docker-compose.build.yml
version: '3.8'

services:
  # macOS build environment (uses Docker Desktop on mac)
  macos-builder:
    build:
      context: .
      dockerfile: Dockerfile.macos
    volumes:
      - .:/ish
      - build-macos:/ish/build
    command: |
      xcodebuild \
        -project iSH.xcodeproj \
        -scheme iSH \
        -destination 'platform=iOS Simulator,name=iPhone 15' \
        build

  # Linux cross-compile environment
  osxcross-builder:
    build:
      context: .
      dockerfile: Dockerfile.osxcross
    volumes:
      - .:/ish
      - build-osxcross:/ish/build
    command: |
      bash -c "
        cd /ish &&
        o64-clang -o build/ish-cli \
          -I. -Iemu -Ikernel \
          main.c emu/aarch64/*.c kernel/aarch64/*.c \
          -DCLI_ONLY=1
      "

  # Linux test environment (QEMU + ARM reference)
  linux-tester:
    build:
      context: .
      dockerfile: Dockerfile.linux-test
    volumes:
      - .:/ish
      - test-results:/ish/test-results
    command: |
      bash -c "
        cd /ish &&
        bash tests/aarch64/tools/reference-check.sh &&
        bash tests/aarch64/e2e/run-e2e-tests.sh
      "

volumes:
  build-macos:
  build-osxcross:
  test-results:
```

## 4. CI/CD Pipeline (GitHub Actions)

```yaml
# .github/workflows/build.yml
name: Build & Test

on: [push, pull_request]

jobs:
  # macOS build and test (full)
  macos:
    runs-on: macos-14
    steps:
      - uses: actions/checkout@v3

      - name: Select Xcode
        run: sudo xcode-select -s /Applications/Xcode_15.0.app

      - name: Build (Simulator)
        run: |
          xcodebuild build \
            -project iSH.xcodeproj \
            -scheme iSH \
            -destination 'platform=iOS Simulator,name=iPhone 15'

      - name: Run Tests
        run: |
          xcodebuild test \
            -project iSH.xcodeproj \
            -scheme "iSH Aarch64 Tests" \
            -destination 'platform=iOS Simulator,name=iPhone 15'

      - name: Upload Build
        uses: actions/upload-artifact@v3
        with:
          name: iSH-iOS-Simulator
          path: build/Debug-iphonesimulator/iSH.app

  # Linux cross-compile (frugality)
  linux-cross:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v3

      - name: Cache osxcross
        uses: actions/cache@v3
        with:
          path: /opt/osxcross
          key: osxcross-${{ runner.os }}

      - name: Setup osxcross
        run: |
          if [ ! -d /opt/osxcross/target ]; then
            git clone https://github.com/tpoechtrager/osxcross.git /tmp/osxcross
            cd /tmp/osxcross
            # SDK extraction requires macOS, use pre-built or skip
            echo "osxcross setup requires macOS SDK"
          fi

      - name: ARM Reference Tests
        run: |
          bash tests/aarch64/tools/reference-check.sh

      - name: Decoder Unit Tests
        run: |
          gcc -I. tests/aarch64/decoder_test.c emu/aarch64/decode.c -o test-decoder
          ./test-decoder

      - name: QEMU E2E Tests
        run: |
          sudo apt-get install qemu-user-static gcc-aarch64-linux-gnu
          bash tests/aarch64/e2e/run-e2e-tests.sh

  # Docker unified build
  docker:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v3

      - name: Build Docker images
        run: docker-compose -f docker-compose.build.yml build

      - name: Run Linux tests
        run: docker-compose -f docker-compose.build.yml run linux-tester
```

## 5. Makefile Targets

```makefile
# Makefile.build

# Detect platform
UNAME := $(shell uname -s)

# macOS builds
ifeq ($(UNAME),Darwin)
BUILD_CMD := xcodebuild
TEST_CMD := xcodebuild test
DEST_SIM := -destination 'platform=iOS Simulator,name=iPhone 15'
DEST_DEVICE := -destination 'generic/platform=iOS'
DEST_MAC := -destination 'platform=macOS'

.PHONY: build-macos build-ios build-sim test-macos

build-macos:
	$(BUILD_CMD) -project iSH.xcodeproj -scheme iSH $(DEST_MAC) build

build-ios:
	$(BUILD_CMD) -project iSH.xcodeproj -scheme iSH $(DEST_DEVICE) build

build-sim:
	$(BUILD_CMD) -project iSH.xcodeproj -scheme iSH $(DEST_SIM) build

test-macos:
	$(TEST_CMD) -project iSH.xcodeproj -scheme "iSH Aarch64 Tests" $(DEST_SIM)

test-ui:
	$(TEST_CMD) -project iSH.xcodeproj -scheme "iSH Aarch64 UI Tests" $(DEST_SIM)

# Linux builds
else
CROSS_PREFIX ?= aarch64-linux-gnu-
QEMU_CMD ?= qemu-aarch64-static

.PHONY: build-linux test-linux test-arm-ref test-qemu

build-linux:
	$(CROSS_PREFIX)gcc -o ish-linux \
		-I. -Iemu -Ikernel \
		main.c emu/aarch64/*.c kernel/aarch64/*.c \
		-DCLI_ONLY=1 -static

test-arm-ref:
	bash tests/aarch64/tools/reference-check.sh

test-qemu:
	bash tests/aarch64/e2e/run-e2e-tests.sh

test-linux: test-arm-ref test-qemu

docker-build:
	docker-compose -f docker-compose.build.yml build

docker-test:
	docker-compose -f docker-compose.build.yml run linux-tester
endif

# Universal targets
.PHONY: clean test all

clean:
	rm -rf build/
	rm -f ish-* test-*

test:
ifeq ($(UNAME),Darwin)
	$(MAKE) test-macos
else
	$(MAKE) test-linux
endif

all: clean test
```

## 6. Development Workflow

### Local Development (macOS)
```bash
# Standard iOS development
open iSH.xcodeproj
# Edit code, Cmd+B to build, Cmd+U to test
```

### CI/CD Development (Linux)
```bash
# Quick validation without macOS
docker-compose -f docker-compose.build.yml run linux-tester

# Or native Linux
make test-linux
```

### Pre-commit Checklist
```bash
# Run on both platforms
make test  # Platform-specific tests
bash tests/aarch64/tools/reference-check.sh  # ARM validation
```

## 7. Cost Comparison

| Approach | macOS Runner | Linux Runner | Time | Cost |
|----------|--------------|--------------|------|------|
| macOS-only | GitHub $0.08/min | - | 10 min | $0.80 |
| Linux-only | - | GitHub $0.008/min | 5 min | $0.04 |
| Hybrid (this plan) | For release | For PRs | 5+10 min | $0.04-0.84 |

**Frugality win**: 90% cost reduction using Linux for PR validation.

## Next Steps

1. [ ] Create `Dockerfile.osxcross` with SDK extraction
2. [ ] Create `Dockerfile.linux-test` with QEMU
3. [ ] Create `docker-compose.build.yml`
4. [ ] Create `.github/workflows/build.yml`
5. [ ] Create `Makefile.build`
6. [ ] Test on both platforms
7. [ ] Document SDK acquisition (legal)
