#!/usr/bin/env bash
# RunVisualTests.sh
# Builds the BambuStudio Linux image (if not already built), builds the
# visual-test image on top of it, runs the tests, and copies results to the
# host.
#
# Usage:
#   ./RunVisualTests.sh [OPTIONS]
#
# Options:
#   --results-dir DIR   Host path to write screenshots/diffs (default: ./visual-test-results)
#   --update-baseline   Overwrite committed baselines with the new screenshots
#   --no-rebuild        Skip rebuilding the main BambuStudio image
#   --help

set -euo pipefail

RESULTS_DIR="./visual-test-results"
UPDATE_BASELINE=0
NO_REBUILD=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --results-dir) RESULTS_DIR="$2"; shift 2 ;;
        --update-baseline) UPDATE_BASELINE=1; shift ;;
        --no-rebuild) NO_REBUILD=1; shift ;;
        --help)
            sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

mkdir -p "$RESULTS_DIR"
RESULTS_ABS=$(realpath "$RESULTS_DIR")

# ---------------------------------------------------------------------------
# 1. Build the main BambuStudio Linux image (unless --no-rebuild)
# ---------------------------------------------------------------------------
if [ "$NO_REBUILD" = "0" ]; then
    echo "=== Building BambuStudio Linux image ==="
    docker build -t bambustudio-linux .
else
    echo "=== Skipping main image build (--no-rebuild) ==="
fi

# ---------------------------------------------------------------------------
# 2. Build the visual-test image
# ---------------------------------------------------------------------------
echo "=== Building visual-test image ==="
docker build -f Dockerfile.visual-test -t bambustudio-visual-test .

# ---------------------------------------------------------------------------
# 3. Run the tests
# ---------------------------------------------------------------------------
echo "=== Running visual tests ==="

EXTRA_MOUNTS=()
if [ "$UPDATE_BASELINE" = "1" ]; then
    BASELINE_ABS=$(realpath "tests/visual/baseline")
    mkdir -p "$BASELINE_ABS"
    EXTRA_MOUNTS+=(-v "$BASELINE_ABS:/tests/visual/baseline")
    echo "(Baseline update mode — screenshots will be written back to tests/visual/baseline)"
fi

docker run --rm \
    -e UPDATE_BASELINE="$UPDATE_BASELINE" \
    -v "$RESULTS_ABS:/results" \
    "${EXTRA_MOUNTS[@]}" \
    bambustudio-visual-test

echo "=== Results saved to: $RESULTS_DIR ==="
