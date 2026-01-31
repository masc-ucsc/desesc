#!/bin/bash
# Gold run regression test for desesc
# This script runs desesc with a known configuration and compares
# the output against stored "golden" results.

set -e

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Bazel puts the binary and data files in runfiles
if [ -n "$TEST_SRCDIR" ]; then
    # Running under bazel test
    RUNFILES="$TEST_SRCDIR/$TEST_WORKSPACE"
    DESESC="$RUNFILES/main/desesc"
    CONF_DIR="$RUNFILES/conf"
    # Run from runfiles root so relative paths in config work
    RUN_DIR="$RUNFILES"
else
    # Running standalone (from project root)
    RUNFILES="$SCRIPT_DIR/.."
    DESESC="$SCRIPT_DIR/../bazel-bin/main/desesc"
    CONF_DIR="$SCRIPT_DIR/../conf"
    RUN_DIR="$SCRIPT_DIR/.."
fi

# Create temp directory for test outputs
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

# Run desesc from the correct directory so relative paths work
cd "$RUN_DIR"

# Run desesc with goldrun1 config, output files go to temp dir
echo "Running desesc with goldrun1 config..."
REPORTFILE=test "$DESESC" -c "$CONF_DIR/goldrun1_desesc.toml"

# Find the generated output files (they are created in current directory)
DESESC_OUTPUT=$(ls desesc_test.* 2>/dev/null | head -1)
if [ -z "$DESESC_OUTPUT" ]; then
    echo "ERROR: desesc output file not found"
    ls -la
    exit 1
fi

# Extract the extension to find matching kanata log
EXT="${DESESC_OUTPUT##*.}"
KANATA_OUTPUT="kanata_log.$EXT"

if [ ! -f "$KANATA_OUTPUT" ]; then
    echo "ERROR: kanata_log.$EXT not found"
    ls -la
    exit 1
fi

echo "Found output files: $DESESC_OUTPUT, $KANATA_OUTPUT"

# Copy output files to temp dir for comparison (cleanup later)
cp "$DESESC_OUTPUT" "$TMPDIR/"
cp "$KANATA_OUTPUT" "$TMPDIR/"

# Also copy files we'll need for cleanup later
CLEANUP_DESESC="$PWD/$DESESC_OUTPUT"
CLEANUP_KANATA="$PWD/$KANATA_OUTPUT"

cd "$TMPDIR"

# Clean up output files from run directory (do it after cd to avoid issues)
rm -f "$CLEANUP_DESESC" "$CLEANUP_KANATA"

# Function to filter and sort desesc output for comparison
# Order can be non-deterministic due to hash map iteration, so we sort
filter_and_sort_desesc_output() {
    grep -v '^OSSim:beginTime=' | \
    grep -v '^OSSim:endTime=' | \
    grep -v '^OSSim:msecs=' | \
    sort
}

# Compare desesc output (filtering non-deterministic fields and sorting)
echo "Comparing desesc output..."
FILTERED_OUTPUT="$TMPDIR/filtered_output.txt"
FILTERED_GOLD="$TMPDIR/filtered_gold.txt"

filter_and_sort_desesc_output < "$DESESC_OUTPUT" > "$FILTERED_OUTPUT"
filter_and_sort_desesc_output < "$CONF_DIR/goldrun1_desesc.result" > "$FILTERED_GOLD"

if ! diff -q "$FILTERED_GOLD" "$FILTERED_OUTPUT" > /dev/null 2>&1; then
    echo "ERROR: desesc output differs from golden result"
    echo "--- Expected (filtered+sorted) ---"
    head -50 "$FILTERED_GOLD"
    echo ""
    echo "--- Actual (filtered+sorted) ---"
    head -50 "$FILTERED_OUTPUT"
    echo ""
    echo "--- Diff ---"
    diff "$FILTERED_GOLD" "$FILTERED_OUTPUT" | head -100 || true
    exit 1
fi

echo "desesc output matches golden result"

# Compare kanata log (order matters here - instruction trace is deterministic)
echo "Comparing kanata log..."
if ! diff -q "$CONF_DIR/goldrun1_kanata_log.result" "$KANATA_OUTPUT" > /dev/null 2>&1; then
    echo "ERROR: kanata log differs from golden result"
    echo "--- Diff (first 50 lines) ---"
    diff "$CONF_DIR/goldrun1_kanata_log.result" "$KANATA_OUTPUT" | head -50 || true
    exit 1
fi

echo "kanata log matches golden result"

echo ""
echo "SUCCESS: All golden run tests passed!"
