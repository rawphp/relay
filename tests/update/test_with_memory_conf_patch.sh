#!/bin/bash
# TDD tests for REQ-017: update.sh --with-memory patches memory_service_autostart in relay.conf
# Run with: bash tests/update/test_with_memory_conf_patch.sh

set -e

RELEASE_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
PASS=0
FAIL=0
ERRORS=()

pass() { echo "  PASS: $1"; PASS=$((PASS+1)); }
fail() { echo "  FAIL: $1"; echo "        $2"; FAIL=$((FAIL+1)); ERRORS+=("$1: $2"); }

assert_contains() {
    local desc="$1" file="$2" pattern="$3"
    if grep -q "$pattern" "$file" 2>/dev/null; then pass "$desc"
    else fail "$desc" "Expected '$pattern' in $file"; fi
}

assert_not_contains() {
    local desc="$1" file="$2" pattern="$3"
    if ! grep -q "$pattern" "$file" 2>/dev/null; then pass "$desc"
    else fail "$desc" "Did not expect '$pattern' in $file"; fi
}

# ── Unit test: conf patch logic (mirrors what update.sh must do) ──────────────

# This is the exact sed command that update.sh must contain:
#   if key exists → replace value; if absent → append
patch_autostart() {
    local conf="$1"
    if grep -q "^memory_service_autostart" "$conf"; then
        sed -i '' 's/^memory_service_autostart = .*/memory_service_autostart = 1/' "$conf" 2>/dev/null || \
        sed -i    's/^memory_service_autostart = .*/memory_service_autostart = 1/' "$conf"
    else
        echo "memory_service_autostart = 1" >> "$conf"
    fi
}

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo ""
echo "=== --with-memory relay.conf patch tests ==="

# Test 1: key present with value 0 → patched to 1
CONF1="$TMP/relay1.conf"
cat > "$CONF1" <<'EOF'
telegram_bot_token = abc
memory_service_autostart = 0
some_other_key = value
EOF
patch_autostart "$CONF1"
assert_contains "key present: autostart patched to 1" "$CONF1" "memory_service_autostart = 1"
assert_not_contains "key present: old value 0 gone" "$CONF1" "memory_service_autostart = 0"

# Test 2: key absent → appended
CONF2="$TMP/relay2.conf"
cat > "$CONF2" <<'EOF'
telegram_bot_token = abc
some_other_key = value
EOF
patch_autostart "$CONF2"
assert_contains "key absent: autostart appended" "$CONF2" "memory_service_autostart = 1"

# Test 3: idempotent — running twice leaves single correct line
CONF3="$TMP/relay3.conf"
cat > "$CONF3" <<'EOF'
telegram_bot_token = abc
memory_service_autostart = 0
EOF
patch_autostart "$CONF3"
patch_autostart "$CONF3"
COUNT=$(grep -c "memory_service_autostart" "$CONF3")
if [ "$COUNT" -eq 1 ]; then
    pass "idempotent: single memory_service_autostart line after two runs"
else
    fail "idempotent: single memory_service_autostart line after two runs" \
         "Found $COUNT occurrences of memory_service_autostart"
fi
assert_contains "idempotent: value is still 1" "$CONF3" "memory_service_autostart = 1"

# ── Structural test: update.sh must contain the patch logic ──────────────────

echo ""
echo "=== update.sh structural test ==="

UPDATE_SH="$RELEASE_DIR/update.sh"

if grep -q "memory_service_autostart = 1" "$UPDATE_SH"; then
    pass "update.sh sets memory_service_autostart = 1 in --with-memory path"
else
    fail "update.sh sets memory_service_autostart = 1 in --with-memory path" \
         "No 'memory_service_autostart = 1' found in update.sh"
fi

# Must check that the patch is inside the WITH_MEMORY block (before daemon restart)
# Verify the patch appears before the restart block
WITH_MEM_LINE=$(grep -n "memory_service_autostart = 1" "$UPDATE_SH" 2>/dev/null | head -1 | cut -d: -f1)
RESTART_LINE=$(grep -n "daemon_was_running.*-eq 1" "$UPDATE_SH" 2>/dev/null | head -1 | cut -d: -f1)

if [ -n "$WITH_MEM_LINE" ] && [ -n "$RESTART_LINE" ] && [ "$WITH_MEM_LINE" -lt "$RESTART_LINE" ]; then
    pass "conf patch occurs before daemon restart"
else
    fail "conf patch occurs before daemon restart" \
         "Patch line=$WITH_MEM_LINE, restart line=$RESTART_LINE"
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
