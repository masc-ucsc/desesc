#!/bin/bash
# Script to update the golden reference files for goldrun tests.
# Run this after making intentional changes to the simulator that
# affect the output.
#
# Usage: ./main/update_golden.sh
#
# This will:
# 1. Build desesc
# 2. Run it with the goldrun1 config
# 3. Update the golden result files in conf/

set -e

# Get the project root directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR/.."

cd "$PROJECT_ROOT"

echo "Building desesc..."
bazel build -c dbg //main:desesc

echo ""
echo "Running desesc with goldrun1 config..."
REPORTFILE=goldrun1 ./bazel-bin/main/desesc -c ./conf/goldrun1_desesc.toml

# Find the generated output files
DESESC_OUTPUT=$(ls desesc_goldrun1.* 2>/dev/null | head -1)
if [ -z "$DESESC_OUTPUT" ]; then
    echo "ERROR: desesc output file not found"
    exit 1
fi

# Extract the extension to find matching kanata log
EXT="${DESESC_OUTPUT##*.}"
KANATA_OUTPUT="kanata_log.$EXT"

if [ ! -f "$KANATA_OUTPUT" ]; then
    echo "ERROR: kanata_log.$EXT not found"
    exit 1
fi

echo "Found output files: $DESESC_OUTPUT, $KANATA_OUTPUT"

# Update golden files
echo "Updating golden files..."
mv "$DESESC_OUTPUT" conf/goldrun1_desesc.result
mv "$KANATA_OUTPUT" conf/goldrun1_kanata_log.result

echo ""
echo "Golden files updated successfully!"
echo "  - conf/goldrun1_desesc.result"
echo "  - conf/goldrun1_kanata_log.result"
echo ""
echo "Don't forget to commit these changes if intended."
