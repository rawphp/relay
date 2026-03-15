#!/bin/bash
# TDD tests for install.sh existing-installation detection.
# Run with: bash tests/install/test_install_detect.sh

set -e

RELEASE_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
PASS=0
FAIL=0
ERRORS=()

pass() { echo "  PASS: $1"; PASS=$((PASS+1)); }
fail() { echo "  FAIL: $1"; echo "        $2"; FAIL=$((FAIL+1)); ERRORS+=("$1: $2"); }

assert_exit_nonzero() {
    local desc="$1"; shift
    if ! "$@" >/dev/null 2>&1; then pass "$desc"
    else fail "$desc" "Expected non-zero exit, got 0"; fi
}

assert_output_contains() {
    local desc="$1" pattern="$2"; shift 2
    local out
    out=$("$@" 2>&1 || true)
    if echo "$out" | grep -q "$pattern"; then pass "$desc"
    else fail "$desc" "Expected '$pattern' in output. Got: $out"; fi
}

assert_exit_zero() {
    local desc="$1"; shift
    if "$@" >/dev/null 2>&1; then pass "$desc"
    else fail "$desc" "Expected zero exit, got non-zero"; fi
}

# ── Setup ──────────────────────────────────────────────────────────────────────
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# Fake HOME so registry writes go to $TMP/.relay
FAKE_HOME="$TMP/home"
mkdir -p "$FAKE_HOME"

INSTALL_SH="$RELEASE_DIR/install.sh"

echo ""
echo "=== install.sh Existing-Installation Detection Tests ==="

# ── Test 1: Registry contains agent name → exit non-zero ──────────────────────
echo ""
echo "--- Already-installed detection ---"

FAKE_HOME_1="$TMP/home1"
mkdir -p "$FAKE_HOME_1"
echo "kai=$FAKE_HOME_1/kai" > "$FAKE_HOME_1/.relay"

# Pipe enter/N for any prompts — script should bail before reaching them
out=$(HOME="$FAKE_HOME_1" bash "$INSTALL_SH" --name kai --home "$FAKE_HOME_1/kai" \
    </dev/null 2>&1 || true)
exit_code=$(HOME="$FAKE_HOME_1" bash "$INSTALL_SH" --name kai --home "$FAKE_HOME_1/kai" \
    </dev/null 2>&1; echo $?)

if HOME="$FAKE_HOME_1" bash "$INSTALL_SH" --name kai --home "$FAKE_HOME_1/kai" \
    </dev/null >/dev/null 2>&1; then
    fail "already-installed agent exits non-zero" "Script exited 0 — should have exited non-zero"
else
    pass "already-installed agent exits non-zero"
fi

# ── Test 2: Output mentions update.sh ─────────────────────────────────────────
FAKE_HOME_2="$TMP/home2"
mkdir -p "$FAKE_HOME_2"
echo "nova=$FAKE_HOME_2/nova" > "$FAKE_HOME_2/.relay"

out=$(HOME="$FAKE_HOME_2" bash "$INSTALL_SH" --name nova --home "$FAKE_HOME_2/nova" \
    </dev/null 2>&1 || true)

if echo "$out" | grep -q "update.sh"; then
    pass "output mentions update.sh when already installed"
else
    fail "output mentions update.sh when already installed" \
         "Expected 'update.sh' in output. Got: $out"
fi

# ── Test 3: Fresh install (no registry) → proceeds past detection ─────────────
echo ""
echo "--- Fresh install detection ---"

FAKE_HOME_3="$TMP/home3"
mkdir -p "$FAKE_HOME_3"
# No .relay file — fresh system

# We only test that it doesn't exit immediately from the detection guard.
# Pass empty stdin so interactive prompts get EOF and bail — that's fine.
# We just want to confirm the detection check itself passes.
out=$(HOME="$FAKE_HOME_3" bash "$INSTALL_SH" --name fresh --home "$FAKE_HOME_3/fresh" \
    </dev/null 2>&1 || true)

if echo "$out" | grep -q "update.sh"; then
    fail "fresh install does not trigger update.sh message" \
         "Unexpected 'update.sh' in fresh-install output. Got: $out"
else
    pass "fresh install does not trigger update.sh message"
fi

# ── Test 4: Different agent name in registry → proceeds past detection ─────────
echo ""
echo "--- Different agent name in registry ---"

FAKE_HOME_4="$TMP/home4"
mkdir -p "$FAKE_HOME_4"
echo "other-agent=$FAKE_HOME_4/other" > "$FAKE_HOME_4/.relay"

out=$(HOME="$FAKE_HOME_4" bash "$INSTALL_SH" --name newagent --home "$FAKE_HOME_4/newagent" \
    </dev/null 2>&1 || true)

if echo "$out" | grep -q "update.sh"; then
    fail "different agent name does not trigger update.sh message" \
         "Unexpected 'update.sh' in output. Got: $out"
else
    pass "different agent name does not trigger update.sh message"
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
