#!/bin/bash
#
# Download Alpine Linux rootfs for iSH
# Run this before building the iOS app
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Get ROOTFS_URL from xcconfig
ROOTFS_URL=$(grep "^ROOTFS_URL =" "$PROJECT_ROOT/app/iSH.xcconfig" | sed 's/ROOTFS_URL = //')

DEST_DIR="$PROJECT_ROOT/deps/rootfs"
DEST_FILE="$DEST_DIR/alpine-rootfs.tar.gz"

# Check if already exists
if [ -f "$DEST_FILE" ]; then
    echo "✓ Rootfs already exists at $DEST_FILE"
    ls -lh "$DEST_FILE"
    exit 0
fi

echo "Downloading Alpine rootfs..."
echo "URL: $ROOTFS_URL"
echo "Destination: $DEST_FILE"

mkdir -p "$DEST_DIR"

# Download with progress
curl -L --fail --progress-bar "$ROOTFS_URL" -o "$DEST_FILE"

echo ""
echo "✓ Download complete!"
ls -lh "$DEST_FILE"
