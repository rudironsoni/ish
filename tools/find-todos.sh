#!/bin/bash
# Find TODO/FIXME comments in source

grep -r "TODO\|FIXME\|XXX" --include="*.c" --include="*.h" emu kernel fs tcti util | grep -v "^Binary" | head -30 || echo "No TODOs found"
