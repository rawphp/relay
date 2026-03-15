#!/bin/bash
# TDD tests for REQ-022: update.sh audit — protected files and README discoverability.
# Run with: bash tests/install/test_update_audit.sh

RELEASE_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
PASS=0
FAIL=0
ERRORS=()

pass() { echo "  PASS: $1"; PASS=$((PASS+1)); }
fail() { echo "  FAIL: $1"; echo "        $2"; FAIL=$((FAIL+1)); ERRORS+=("$1: $2"); }

UPDATE_SH="$RELEASE_DIR/update.sh"
README="$RELEASE_DIR/README.md"

echo ""
echo "=== update.sh Protected Files Tests ==="

# update.sh must not have unconditional cp commands targeting identity files or relay.conf
for protected in "IDENTITY.md" "USER.md" "SOUL.md" "PRIORITIES.md" "relay.conf"; do
    if grep -E "^\s*cp .*$protected" "$UPDATE_SH" 2>/dev/null | grep -qv '^\s*#'; then
        fail "update.sh does not overwrite $protected" \
             "Found unconditional cp command targeting $protected"
    else
        pass "update.sh does not overwrite $protected"
    fi
done

echo ""
echo "=== update.sh Directory Creation Tests ==="

# update.sh must mkdir scripts/maintenance before copying to it
if grep -q 'mkdir -p.*scripts/maintenance' "$UPDATE_SH" 2>/dev/null; then
    pass "update.sh creates scripts/maintenance before deploy"
else
    fail "update.sh creates scripts/maintenance before deploy" \
         "No 'mkdir -p .*/scripts/maintenance' found — cp will fail on fresh agents"
fi

echo ""
echo "=== update.sh Stop-Before-Deploy Tests ==="

# update.sh must unload plist (stop) before copying the binary
# Verify "launchctl unload" appears before "cp $DAEMON_BIN"
unload_line=$(grep -n "launchctl unload" "$UPDATE_SH" | head -1 | cut -d: -f1)
cp_bin_line=$(grep -n 'cp "\$DAEMON_BIN"' "$UPDATE_SH" | head -1 | cut -d: -f1)

if [ -n "$unload_line" ] && [ -n "$cp_bin_line" ] && [ "$unload_line" -lt "$cp_bin_line" ]; then
    pass "daemon is stopped before binary is deployed"
else
    fail "daemon is stopped before binary is deployed" \
         "launchctl unload (line $unload_line) should appear before cp DAEMON_BIN (line $cp_bin_line)"
fi

echo ""
echo "=== update.sh Restart Tests ==="

# update.sh must restart the daemon after deploy
if grep -q "launchctl load" "$UPDATE_SH" 2>/dev/null; then
    pass "update.sh restarts daemon via launchctl load"
else
    fail "update.sh restarts daemon via launchctl load" \
         "No 'launchctl load' found in update.sh"
fi

echo ""
echo "=== README Discoverability Tests ==="

if grep -q "## Updating" "$README" 2>/dev/null; then
    pass "README has Updating section"
else
    fail "README has Updating section" "No '## Updating' heading found in README.md"
fi

if grep -q "update.sh" "$README" 2>/dev/null; then
    pass "README mentions update.sh"
else
    fail "README mentions update.sh" "No reference to 'update.sh' in README.md"
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
