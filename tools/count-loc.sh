#!/bin/bash
# Count lines of code (excluding deps)

find . -name "*.c" -o -name "*.h" | grep -v "^./deps/" | xargs wc -l | tail -1
