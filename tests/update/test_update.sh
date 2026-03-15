#!/bin/bash
# TDD tests for update.sh plist regeneration and skills install behavior.
# Run with: bash tests/update/test_update.sh
#
# These tests verify the sed-based plist generation logic and that
# update.sh covers all deployable artifacts.

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

assert_file_exists() {
    local desc="$1" file="$2"
    if [ -f "$file" ]; then pass "$desc"
    else fail "$desc" "File not found: $file"; fi
}

assert_executable() {
    local desc="$1" file="$2"
    if [ -x "$file" ]; then pass "$desc"
    else fail "$desc" "Not executable: $file"; fi
}

# The plist generation function (mirrors update.sh's logic)
generate_plist() {
    local template="$1" dest="$2" relay_home="$3" label="$4" path_val="$5"
    sed \
        -e "s|__RELAY_HOME__|$relay_home|g" \
        -e "s|__PLIST_LABEL__|$label|g" \
        -e "s|__PATH__|$path_val|g" \
        "$template" > "$dest"
}

# ── Setup ──────────────────────────────────────────────────────────────────────
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

FAKE_RELAY_HOME="$TMP/relay"
FAKE_LABEL="com.testuser.relay"
FAKE_PATH="/usr/local/bin:/usr/bin:/bin"

mkdir -p "$FAKE_RELAY_HOME"/{config,skills,bin,lib/memory}

echo ""
echo "=== Plist Generation Tests ==="

# ── memory-index plist ─────────────────────────────────────────────────────────
MI_TEMPLATE="$RELEASE_DIR/templates/config/memory_index.plist.template"
MI_GENERATED="$TMP/com.testuser.relay.memory-index.plist"

generate_plist "$MI_TEMPLATE" "$MI_GENERATED" "$FAKE_RELAY_HOME" "$FAKE_LABEL" "$FAKE_PATH"

assert_file_exists "memory-index plist is generated" "$MI_GENERATED"
assert_contains "RELAY_HOME placeholder replaced" "$MI_GENERATED" "$FAKE_RELAY_HOME"
assert_not_contains "no __RELAY_HOME__ placeholders remain" "$MI_GENERATED" "__RELAY_HOME__"
assert_not_contains "no __PLIST_LABEL__ placeholders remain" "$MI_GENERATED" "__PLIST_LABEL__"
assert_not_contains "no __PATH__ placeholders remain" "$MI_GENERATED" "__PATH__"
assert_contains "OMP_NUM_THREADS present in memory-index plist" "$MI_GENERATED" "OMP_NUM_THREADS"
assert_contains "OMP_NUM_THREADS value is 2" "$MI_GENERATED" "<string>2</string>"
assert_contains "MKL_NUM_THREADS present in memory-index plist" "$MI_GENERATED" "MKL_NUM_THREADS"
assert_contains "TOKENIZERS_PARALLELISM present in memory-index plist" "$MI_GENERATED" "TOKENIZERS_PARALLELISM"
assert_contains "TOKENIZERS_PARALLELISM value is false" "$MI_GENERATED" "<string>false</string>"
assert_contains "venv python used (not system python)" "$MI_GENERATED" "lib/memory/venv/bin/python3"

# ── Plist overwrite test ───────────────────────────────────────────────────────
echo ""
echo "=== Plist Overwrite Tests (existing install) ==="

# Simulate a stale installed plist (old version without env vars)
STALE_PLIST="$TMP/stale.memory-index.plist"
echo "<plist>old content</plist>" > "$STALE_PLIST"
assert_contains "stale plist has old content before update" "$STALE_PLIST" "old content"

# After regeneration (what update.sh should do now — always overwrite)
generate_plist "$MI_TEMPLATE" "$STALE_PLIST" "$FAKE_RELAY_HOME" "$FAKE_LABEL" "$FAKE_PATH"

assert_not_contains "stale content is gone after regeneration" "$STALE_PLIST" "old content"
assert_contains "new env vars present after regeneration" "$STALE_PLIST" "OMP_NUM_THREADS"

# ── Skills install test ────────────────────────────────────────────────────────
echo ""
echo "=== Skills Install Tests ==="

SKILLS_SRC="$RELEASE_DIR/skills"

# Simulate what update.sh should do for skills
cp "$SKILLS_SRC"/*.sh "$FAKE_RELAY_HOME/skills/" 2>/dev/null || true
cp "$SKILLS_SRC"/*.md "$FAKE_RELAY_HOME/skills/" 2>/dev/null || true
chmod +x "$FAKE_RELAY_HOME"/skills/*.sh 2>/dev/null || true

assert_file_exists "speak.sh copied to skills" "$FAKE_RELAY_HOME/skills/speak.sh"
assert_file_exists "browser.sh copied to skills" "$FAKE_RELAY_HOME/skills/browser.sh"
assert_file_exists "speak-telegram.sh copied to skills" "$FAKE_RELAY_HOME/skills/speak-telegram.sh"
assert_file_exists "speak.md copied to skills" "$FAKE_RELAY_HOME/skills/speak.md"
assert_executable "speak.sh is executable" "$FAKE_RELAY_HOME/skills/speak.sh"
assert_executable "browser.sh is executable" "$FAKE_RELAY_HOME/skills/browser.sh"

# ── Template completeness check ────────────────────────────────────────────────
echo ""
echo "=== Template Placeholder Tests ==="

for template in "$RELEASE_DIR"/templates/config/*.plist.template; do
    name=$(basename "$template")
    assert_contains "$name has __RELAY_HOME__ placeholder" "$template" "__RELAY_HOME__"
    assert_contains "$name has __PLIST_LABEL__ placeholder" "$template" "__PLIST_LABEL__"
done

# ── update.sh structural tests ────────────────────────────────────────────────
echo ""
echo "=== update.sh Structural Tests ==="

UPDATE_SH="$RELEASE_DIR/update.sh"

# update.sh must NOT use "if not already present" guard for MI/OT plists.
# The variables used are MI_PLIST_PATH and OT_PLIST_PATH.
if grep -q '! -f.*MI_PLIST_PATH' "$UPDATE_SH"; then
    fail "update.sh regenerates memory-index plist unconditionally" \
         "Found '! -f \$MI_PLIST_PATH' guard — plist only installed if missing, never updated"
else
    pass "update.sh regenerates memory-index plist unconditionally"
fi

# update.sh must copy skills
if grep -q 'skills' "$UPDATE_SH"; then
    pass "update.sh includes skills copy step"
else
    fail "update.sh includes skills copy step" \
         "No reference to 'skills' found in update.sh"
fi

# update.sh must check if the process is actually running before setting daemon_was_running
# (not just whether the plist file exists)
if grep -q 'kill -0.*pid\|launchctl list.*plist_label\|daemon_was_running.*running' "$UPDATE_SH"; then
    pass "update.sh checks if daemon is actually running before marking daemon_was_running"
else
    fail "update.sh checks if daemon is actually running before marking daemon_was_running" \
         "daemon_was_running=1 is set without verifying the process is alive"
fi

# update.sh restart block must exist (stop then start after deploy)
if grep -q 'daemon_was_running.*-eq 1' "$UPDATE_SH"; then
    pass "update.sh restarts daemon when it was running before update"
else
    fail "update.sh restarts daemon when it was running before update" \
         "No restart block guarded by daemon_was_running found in update.sh"
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
