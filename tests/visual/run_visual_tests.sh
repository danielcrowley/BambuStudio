#!/usr/bin/env bash
# run_visual_tests.sh
# Launches BambuStudio inside an Xvfb virtual display, runs a set of visual
# tests (launch smoke test, Prepare tab test, …), and compares screenshots
# against committed baselines.
#
# Environment variables:
#   RESULTS_DIR      Where to write screenshots / diffs  (default: /results)
#   TIMEOUT          Seconds to wait for the window      (default: 45)
#   UPDATE_BASELINE  Set to 1 to overwrite baselines     (default: unset)

set -euo pipefail

BINARY="/BambuStudio/build/package/bin/bambu-studio"
RESULTS_DIR="${RESULTS_DIR:-/results}"
BASELINE_DIR="/tests/visual/baseline"
TIMEOUT="${TIMEOUT:-45}"
DISPLAY_NUM=":99"

mkdir -p "$RESULTS_DIR" "$BASELINE_DIR"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*" >&2; EXIT_CODE=1; }
EXIT_CODE=0

cleanup() {
    kill "$APP_PID"  2>/dev/null || true
    kill "$XVFB_PID" 2>/dev/null || true
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# 1. Start Xvfb
# ---------------------------------------------------------------------------
echo "--- Starting Xvfb on $DISPLAY_NUM ---"
Xvfb "$DISPLAY_NUM" -screen 0 1920x1080x24 +extension GLX &
XVFB_PID=$!
export DISPLAY="$DISPLAY_NUM"

# Wait until Xvfb accepts connections (up to 5 s)
for i in $(seq 1 10); do
    xdpyinfo -display "$DISPLAY_NUM" >/dev/null 2>&1 && break
    sleep 0.5
done

# ---------------------------------------------------------------------------
# 2. Configure software OpenGL (no GPU inside the container)
# ---------------------------------------------------------------------------
export LIBGL_ALWAYS_SOFTWARE=1
export MESA_GL_VERSION_OVERRIDE=3.3
export MESA_GLSL_VERSION_OVERRIDE=330

# ---------------------------------------------------------------------------
# 3. Prepare a fresh config dir so first-run dialogs are predictable
# ---------------------------------------------------------------------------
TMPCONF=$(mktemp -d)
export HOME="$TMPCONF"
mkdir -p "$HOME/.config"

# ---------------------------------------------------------------------------
# 4. Launch BambuStudio
# ---------------------------------------------------------------------------
echo "--- Launching BambuStudio ---"
"$BINARY" &
APP_PID=$!

# ---------------------------------------------------------------------------
# 5. Wait for the main window
# ---------------------------------------------------------------------------
echo "--- Waiting up to ${TIMEOUT}s for window ---"
WINDOW_ID=""
for i in $(seq 1 "$TIMEOUT"); do
    WINDOW_ID=$(xdotool search --name "BambuStudio" 2>/dev/null | head -1 || true)
    if [ -n "$WINDOW_ID" ]; then
        echo "Window found (id=$WINDOW_ID) after ${i}s"
        break
    fi
    # Bail early if the app already exited
    if ! kill -0 "$APP_PID" 2>/dev/null; then
        fail "BambuStudio process exited before the window appeared"
        exit 1
    fi
    sleep 1
done

if [ -z "$WINDOW_ID" ]; then
    fail "BambuStudio window did not appear within ${TIMEOUT}s"
    exit 1
fi

# Give the UI a moment to finish painting
sleep 3

# ---------------------------------------------------------------------------
# 6. Capture screenshot
# ---------------------------------------------------------------------------
SCREENSHOT="$RESULTS_DIR/launch.png"
echo "--- Taking screenshot → $SCREENSHOT ---"
scrot --display="$DISPLAY_NUM" "$SCREENSHOT"

# ---------------------------------------------------------------------------
# 7. Compare against baseline (or create one)
# ---------------------------------------------------------------------------
BASELINE="$BASELINE_DIR/launch.png"

if [ "${UPDATE_BASELINE:-0}" = "1" ] || [ ! -f "$BASELINE" ]; then
    cp "$SCREENSHOT" "$BASELINE"
    echo "Baseline saved/updated: $BASELINE"
    pass "launch test — baseline written"
else
    echo "--- Comparing against baseline ---"
    DIFF_FILE="$RESULTS_DIR/launch_diff.png"

    # compare exits 1 when images differ, 2 on error — capture metric only
    DIFF_METRIC=$(compare -metric RMSE -fuzz 5% \
        "$BASELINE" "$SCREENSHOT" "$DIFF_FILE" 2>&1 | awk '{print $1}' || true)

    echo "RMSE diff: $DIFF_METRIC"

    # Treat RMSE > 10 as a failure (0–255 scale)
    THRESHOLD=10
    if awk "BEGIN { exit ($DIFF_METRIC > $THRESHOLD) ? 0 : 1 }" 2>/dev/null; then
        fail "launch test — RMSE $DIFF_METRIC exceeds threshold $THRESHOLD (diff: $DIFF_FILE)"
    else
        pass "launch test — RMSE $DIFF_METRIC within threshold $THRESHOLD"
    fi
fi

# ---------------------------------------------------------------------------
# 8. Prepare tab test — click the tab, screenshot, compare baseline
# ---------------------------------------------------------------------------
echo "--- Opening Prepare tab ---"
if python3 /tests/visual/click_tab.py "$WINDOW_ID" "Prepare"; then
    sleep 3   # let the 3-D editor finish painting

    PREPARE_SHOT="$RESULTS_DIR/prepare_tab.png"
    echo "--- Taking Prepare-tab screenshot → $PREPARE_SHOT ---"
    scrot --display="$DISPLAY_NUM" "$PREPARE_SHOT"

    PREPARE_BASELINE="$BASELINE_DIR/prepare_tab.png"
    if [ "${UPDATE_BASELINE:-0}" = "1" ] || [ ! -f "$PREPARE_BASELINE" ]; then
        cp "$PREPARE_SHOT" "$PREPARE_BASELINE"
        echo "Baseline saved/updated: $PREPARE_BASELINE"
        pass "prepare tab test — baseline written"
    else
        echo "--- Comparing Prepare tab against baseline ---"
        PREPARE_DIFF="$RESULTS_DIR/prepare_tab_diff.png"
        DIFF_METRIC=$(compare -metric RMSE -fuzz 5% \
            "$PREPARE_BASELINE" "$PREPARE_SHOT" "$PREPARE_DIFF" 2>&1 | awk '{print $1}' || true)
        echo "RMSE diff: $DIFF_METRIC"
        THRESHOLD=10
        if awk "BEGIN { exit ($DIFF_METRIC > $THRESHOLD) ? 0 : 1 }" 2>/dev/null; then
            fail "prepare tab test — RMSE $DIFF_METRIC exceeds threshold $THRESHOLD (diff: $PREPARE_DIFF)"
        else
            pass "prepare tab test — RMSE $DIFF_METRIC within threshold $THRESHOLD"
        fi
    fi
else
    fail "prepare tab test — could not locate 'Prepare' tab via OCR (see stderr above)"
fi

echo "--- Results written to $RESULTS_DIR ---"
exit "$EXIT_CODE"
