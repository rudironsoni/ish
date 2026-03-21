# iSH Build Makefile
# Pre-build setup and iOS build automation

.PHONY: all download-rootfs build-ios build-sim clean test

# Default target
all: download-rootfs build-ios

# Download Alpine rootfs (run once or when updating)
download-rootfs:
	@echo "=== Downloading Alpine rootfs ==="
	@./scripts/download-rootfs.sh

# Build for iOS device (requires connected device)
build-ios: download-rootfs
	@echo "=== Building for iOS device ==="
	xcodebuild -project iSH.xcodeproj -scheme iSH -configuration Release \
		-destination 'generic/platform=iOS' build

# Build for iOS Simulator
build-sim: download-rootfs
	@echo "=== Building for iOS Simulator ==="
	xcodebuild -project iSH.xcodeproj -scheme iSH -configuration Release \
		-destination 'platform=iOS Simulator,name=iPhone 17 Pro Max' build

# Create IPA for distribution
ipa: download-rootfs
	@echo "=== Creating IPA ==="
	@mkdir -p Payload
	@rm -rf Payload/*
	@xcodebuild -project iSH.xcodeproj -scheme iSH -configuration Release \
		-archivePath /tmp/iSH.xcarchive archive
	@echo "IPA creation complete"

# Run all tests
test:
	@echo "=== Running tests ==="
	@./tests/aarch64/run_all_tests.sh

# Clean build artifacts
clean:
	@echo "=== Cleaning ==="
	@rm -rf build
	@rm -rf deps/rootfs/*.tar.gz
	@rm -rf Payload iSH.ipa
	@echo "Clean complete"

# Full clean (including Xcode derived data)
distclean: clean
	@echo "=== Deep cleaning ==="
	@rm -rf ~/Library/Developer/Xcode/DerivedData/iSH-*
	@echo "Deep clean complete"

# Help
help:
	@echo "iSH Build Targets:"
	@echo "  make download-rootfs  - Download Alpine rootfs (run once)"
	@echo "  make build-ios        - Build for iOS device"
	@echo "  make build-sim        - Build for iOS Simulator"
	@echo "  make ipa              - Create IPA archive"
	@echo "  make test             - Run test suite"
	@echo "  make clean            - Clean build artifacts"
	@echo "  make distclean        - Full clean including Xcode cache"
