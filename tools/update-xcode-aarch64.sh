#!/bin/bash
# Update Xcode project to include new aarch64 C files
# Run this on macOS to modify iSH.xcodeproj/project.pbxproj

set -e

PROJECT="iSH.xcodeproj/project.pbxproj"
BACKUP="${PROJECT}.backup"

if [ ! -f "$PROJECT" ]; then
    echo "ERROR: $PROJECT not found"
    exit 1
fi

echo "Creating backup: $BACKUP"
cp "$PROJECT" "$BACKUP"

echo "Updating Xcode project for aarch64 C files..."

# Check if already updated
if grep -q "asbestos/aarch64" "$PROJECT"; then
    echo "Xcode project already has asbestos/aarch64 references"
    exit 0
fi

echo ""
echo "Manual steps required:"
echo "1. Open iSH.xcodeproj in Xcode"
echo "2. Right-click 'asbestos' folder in Navigator"
echo "3. Select 'Add Files to iSH...'"
echo "4. Select the 'asbestos/aarch64/' folder"
echo "5. Ensure 'Create groups' is selected"
echo "6. Check 'Copy items if needed'"
echo "7. Click 'Add'"
echo ""
echo "Files to add:"
ls -1 asbestos/aarch64/*.c asbestos/aarch64/*.h 2>/dev/null || echo "  (no files found)"
echo ""
echo "After adding, the project will build with TCTI gadgets instead of assembly"
