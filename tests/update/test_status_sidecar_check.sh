#!/bin/bash
# TDD tests for REQ-018: remove stale memory-index LaunchAgent status,
# replace with live pgrep check for memory_http.py process.
# Run with: bash tests/update/test_status_sidecar_check.sh

set -e

RELEASE_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
PASS=0
FAIL=0
ERRORS=()

pass() { echo "  PASS: $1"; PASS=$((PASS+1)); }
fail() { echo "  FAIL: $1"; echo "        $2"; FAIL=$((FAIL+1)); ERRORS+=("$1: $2"); }

UPDATE_SH="$RELEASE_DIR/update.sh"

echo ""
echo "=== REQ-018: memory-index status removal tests ==="

# Test 1: memory-index label must NOT appear in the for-label loop
if grep -q '"$plist_label.memory-index"' "$UPDATE_SH"; then
    fail "memory-index removed from for-label loop" \
         "Found \"\$plist_label.memory-index\" still in update.sh"
else
    pass "memory-index removed from for-label loop"
fi

# Test 2: update.sh must use pgrep to check memory sidecar in status block
# The status block is after the "Service status:" echo
STATUS_BLOCK=$(awk '/Service status:/,/^fi$/' "$UPDATE_SH")
if echo "$STATUS_BLOCK" | grep -q 'pgrep.*memory_http'; then
    pass "status block uses pgrep for memory sidecar check"
else
    fail "status block uses pgrep for memory sidecar check" \
         "No pgrep memory_http found in the Service status block"
fi

# Test 3: status block must guard on apps/memory-py existing (not always print)
if echo "$STATUS_BLOCK" | grep -q 'apps/memory-py'; then
    pass "status block checks apps/memory-py before printing sidecar status"
else
    fail "status block checks apps/memory-py before printing sidecar status" \
         "No apps/memory-py guard found in the Service status block"
fi

# ── Summary ────────────────────────────────────────────────────────────────────
echo ""
echo "────────────────────────────────"
echo "Ran $((PASS+FAIL)) tests: $PASS passed, $FAIL failed"
if [ $FAIL -gt 0 ]; then
    echo ""
    echo "FAILURES:"
    for err in "${ERRORS[@]}"; do echo "  - $err"; done
    echo ""
    exit 1
else
    echo "OK"
    echo ""
fi
