#!/bin/bash
# relay Doctor — diagnoses a relay agent installation
# Checks every layer: registry, config, binary, Claude invocation, daemons, logs
#
# Usage:
#   ./scripts/doctor.sh              # diagnose all agents in ~/.relay
#   ./scripts/doctor.sh nova         # diagnose a specific agent

set -o pipefail

# ── Colors ────────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
DIM='\033[2m'
NC='\033[0m'

ok()      { echo -e "  ${GREEN}✓${NC} $1"; PASS=$((PASS+1)); }
fail()    { echo -e "  ${RED}✗${NC} $1"; FAIL=$((FAIL+1)); }
warn()    { echo -e "  ${YELLOW}!${NC} $1"; WARN=$((WARN+1)); }
info()    { echo -e "  ${BLUE}·${NC} $1"; }
section() { echo -e "\n${BOLD}── $1${NC}"; }

conf_get() {
    grep "^${1}" "$CONF" 2>/dev/null | cut -d= -f2- | tr -d ' '
}

REGISTRY="$HOME/.relay"
TARGET="${1:-}"
PASS=0; FAIL=0; WARN=0

# ── Header ────────────────────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}relay Doctor${NC}  $(date '+%Y-%m-%d %H:%M:%S')"
echo ""

# ── Registry ──────────────────────────────────────────────────────────────────
section "Registry"

if [ -f "$REGISTRY" ]; then
    ok "~/.relay exists"
    info "Contents:"
    grep -v '^#' "$REGISTRY" | grep '=' | while IFS='=' read -r n h; do
        [ -z "$n" ] && continue
        echo -e "      ${DIM}$n = $h${NC}"
    done
else
    fail "~/.relay not found — run install.sh first"
    exit 1
fi

# ── Build agent list ───────────────────────────────────────────────────────────
declare -a AGENT_NAMES
declare -a AGENT_HOMES

while IFS='=' read -r name home; do
    [[ "$name" =~ ^[[:space:]]*# ]] && continue
    [[ -z "$name" ]] && continue
    name=$(echo "$name" | tr -d ' ')
    home=$(echo "$home" | tr -d ' ')
    home="${home/#\~/$HOME}"
    if [ -n "$TARGET" ] && [ "$name" != "$TARGET" ]; then
        continue
    fi
    AGENT_NAMES+=("$name")
    AGENT_HOMES+=("$home")
done < "$REGISTRY"

if [ "${#AGENT_NAMES[@]}" -eq 0 ]; then
    if [ -n "$TARGET" ]; then
        fail "Agent '$TARGET' not found in ~/.relay"
    else
        fail "No agents in ~/.relay"
    fi
    exit 1
fi

# ── Diagnose each agent ────────────────────────────────────────────────────────
for i in "${!AGENT_NAMES[@]}"; do
    AGENT="${AGENT_NAMES[$i]}"
    HOME_DIR="${AGENT_HOMES[$i]}"
    CONF="$HOME_DIR/config/relay.conf"
    PASS=0; FAIL=0; WARN=0

    echo ""
    echo -e "${BOLD}━━━ Agent: $AGENT @ $HOME_DIR ━━━${NC}"

    # ── Directory structure ────────────────────────────────────────────────────
    section "Directory Structure"

    if [ -d "$HOME_DIR" ]; then
        ok "$HOME_DIR exists"
    else
        fail "$HOME_DIR does not exist"
        continue
    fi

    for d in bin config logs "lib/memory" scripts/maintenance; do
        if [ -d "$HOME_DIR/$d" ]; then
            ok "$d/"
        else
            fail "$d/ missing"
        fi
    done

    # ── Config ────────────────────────────────────────────────────────────────
    section "Config (relay.conf)"

    if [ -f "$CONF" ]; then
        ok "relay.conf exists"
        perm=$(stat -f "%OLp" "$CONF" 2>/dev/null || stat -c "%a" "$CONF" 2>/dev/null)
        if [ "$perm" = "600" ]; then
            ok "permissions: 600 (private)"
        else
            fail "permissions: $perm (should be 600)"
        fi
    else
        fail "relay.conf not found at $CONF"
        continue
    fi

    agent_name=$(conf_get "agent_name")
    user_name=$(conf_get "user_name")
    tg_token=$(conf_get "telegram_bot_token")
    tg_uid=$(conf_get "telegram_user_id")
    claude_bin=$(conf_get "claude_binary" | sed "s|^~|$HOME|")
    workspace=$(conf_get "workspace_path" | sed "s|^~|$HOME|")
    pid_file=$(conf_get "pid_file")
    llm_provider=$(conf_get "llm_provider")
    memory_enabled=$(conf_get "memory_search_enabled")

    info "agent_name     = ${agent_name:-[missing]}"
    info "user_name      = ${user_name:-[missing]}"

    if [ -n "$tg_token" ] && [ "$tg_token" != "YOUR_BOT_TOKEN_FROM_BOTFATHER" ]; then
        info "telegram_token = [set — ${#tg_token} chars]"
    else
        fail "telegram_bot_token not configured"
    fi

    if [ -n "$tg_uid" ] && [ "$tg_uid" != "YOUR_TELEGRAM_USER_ID" ]; then
        info "telegram_uid   = $tg_uid"
    else
        fail "telegram_user_id not configured"
    fi

    info "llm_provider   = ${llm_provider:-claude}"
    info "claude_binary  = $claude_bin"
    info "workspace_path = $workspace"
    info "pid_file       = ${pid_file:-[not set]}"
    info "memory_search  = ${memory_enabled:-1}"

    if [ -n "$workspace" ]; then
        if [ -d "$workspace" ]; then
            ok "workspace_path directory exists"
        else
            fail "workspace_path '$workspace' does not exist"
        fi
    fi

    # ── Identity files ─────────────────────────────────────────────────────────
    section "Identity Files"

    for f in SOUL.md IDENTITY.md USER.md PRIORITIES.md MEMORY.md; do
        path="$HOME_DIR/$f"
        if [ -f "$path" ]; then
            size=$(wc -c < "$path" | tr -d ' ')
            ok "$f (${size} bytes)"
        else
            warn "$f missing"
        fi
    done

    # ── Claude binary ──────────────────────────────────────────────────────────
    section "Claude Binary"

    if [ -z "$claude_bin" ]; then
        fail "claude_binary not set in relay.conf"
    elif [ ! -f "$claude_bin" ]; then
        fail "binary not found: $claude_bin"
    elif [ ! -x "$claude_bin" ]; then
        fail "binary not executable: $claude_bin"
    else
        ok "binary exists: $claude_bin"
        version=$("$claude_bin" --version 2>&1 | head -1)
        if [ $? -eq 0 ]; then
            ok "version: $version"
        else
            fail "claude --version failed: $version"
        fi
    fi

    # ── Claude invocation test ─────────────────────────────────────────────────
    section "Claude Invocation Test (daemon flags)"

    if [ -f "$claude_bin" ] && [ -x "$claude_bin" ] && [ -d "$workspace" ]; then
        info "Running: $claude_bin -p 'ping' --output-format json --dangerously-skip-permissions --add-dir $workspace"
        test_out=$("$claude_bin" -p "ping" \
            --output-format json \
            --dangerously-skip-permissions \
            --add-dir "$workspace" \
            --system-prompt "Reply with exactly the word: pong" \
            2>&1)
        test_rc=$?
        info "exit_code = $test_rc"
        info "output (first 200 chars): ${test_out:0:200}"
        if [ $test_rc -eq 0 ] && echo "$test_out" | grep -q '"result"'; then
            ok "Full daemon invocation: OK"
        elif [ $test_rc -eq 0 ]; then
            warn "Exit 0 but no 'result' key in output — check JSON format"
        else
            fail "Daemon invocation failed (exit $test_rc)"
        fi
    else
        warn "Skipping invocation test (binary or workspace not ready)"
    fi

    # ── LaunchAgents ───────────────────────────────────────────────────────────
    section "LaunchAgents"

    import getpass 2>/dev/null || true
    user=$(whoami)
    base_label="com.${user}"

    if [ -f "$HOME/Library/LaunchAgents/$base_label.relay-${AGENT}.plist" ]; then
        plist_label="$base_label.relay-$AGENT"
    else
        plist_label="$base_label.relay"
    fi

    la_dir="$HOME/Library/LaunchAgents"
    for suffix in "" ".memory-index"; do
        plist_path="$la_dir/${plist_label}${suffix}.plist"
        label="${plist_label}${suffix}"
        if [ -f "$plist_path" ]; then
            ok "plist exists: $label"
            # Check PYTHONUNBUFFERED for sub-daemons
            if [ -n "$suffix" ]; then
                if grep -q "PYTHONUNBUFFERED" "$plist_path"; then
                    ok "PYTHONUNBUFFERED=1 set in plist"
                else
                    fail "PYTHONUNBUFFERED missing — stdout will be buffered (run update.sh to fix)"
                fi
            fi
            pid=$(launchctl list 2>/dev/null | awk -v lbl="$label" '$3 == lbl {print $1}')
            exit_code=$(launchctl list 2>/dev/null | awk -v lbl="$label" '$3 == lbl {print $2}')
            if [ -n "$pid" ] && [ "$pid" != "-" ]; then
                ok "loaded: $label (PID $pid)"
            elif [ -n "$exit_code" ] && [ "$exit_code" != "0" ]; then
                fail "not running, last exit code: $exit_code — $label"
            else
                warn "not running: $label"
            fi
        else
            warn "plist not found: $plist_path"
        fi
    done

    # ── Processes ─────────────────────────────────────────────────────────────
    section "Processes"

    actual_pid_file="${pid_file:-/tmp/relay-${AGENT}.pid}"
    actual_pid_file="${actual_pid_file/#\~/$HOME}"

    if [ -f "$actual_pid_file" ]; then
        relay_pid=$(cat "$actual_pid_file" 2>/dev/null)
        if [ -n "$relay_pid" ] && kill -0 "$relay_pid" 2>/dev/null; then
            ok "relay running (PID $relay_pid)"
        else
            fail "PID file exists ($actual_pid_file = $relay_pid) but process is dead"
        fi
    else
        fail "PID file not found: $actual_pid_file"
    fi

    watchdog_count=$(ps aux | grep -v grep | grep "relay-watchdog" | grep "$HOME_DIR" | wc -l | tr -d ' ')
    if [ "$watchdog_count" -gt 0 ]; then
        ok "watchdog running"
    else
        warn "watchdog not running"
    fi

    mi_count=$(ps aux | grep -v grep | grep "memory_index_daemon" | grep "$HOME_DIR" | wc -l | tr -d ' ')
    [ "$mi_count" -gt 0 ] && ok "memory_index_daemon running" || warn "memory_index_daemon not running"

    # ── Python / shared venv ───────────────────────────────────────────────────
    section "Python / Shared Venv"

    SHARED_VENV="$HOME/.relay-shared/venv"
    if [ -d "$SHARED_VENV" ]; then
        ok "~/.relay-shared/venv exists"
        py="$SHARED_VENV/bin/python3"
        info "python: $($py --version 2>&1)"

        for pkg in sentence_transformers faiss-cpu fastapi uvicorn; do
            if "$SHARED_VENV/bin/pip" show "$pkg" >/dev/null 2>&1; then
                version=$("$SHARED_VENV/bin/pip" show "$pkg" 2>/dev/null | grep "^Version:" | cut -d' ' -f2)
                ok "$pkg $version"
            else
                if [ "$pkg" = "fastapi" ] || [ "$pkg" = "uvicorn" ]; then
                    warn "$pkg not installed (dashboard won't work)"
                else
                    fail "$pkg not installed"
                fi
            fi
        done
    else
        fail "~/.relay-shared/venv not found"
    fi

    # ── Logs ──────────────────────────────────────────────────────────────────
    section "Logs"

    for log_name in "logs/relay.log"; do
        log_path="$HOME_DIR/$log_name"
        if [ -f "$log_path" ]; then
            size=$(wc -c < "$log_path" | tr -d ' ')
            lines=$(wc -l < "$log_path" | tr -d ' ')
            last=$(tail -1 "$log_path" 2>/dev/null | cut -c1-60)
            if [ "$size" -eq 0 ]; then
                fail "$log_name: EMPTY (0 bytes)"
            else
                ok "$log_name: ${size} bytes / ${lines} lines"
                info "  last: $last"
            fi
        else
            warn "$log_name: does not exist yet"
        fi
    done

    echo ""
    info "Last 5 relay.log entries:"
    tail -5 "$HOME_DIR/logs/relay.log" 2>/dev/null | while read -r line; do
        echo -e "    ${DIM}${line}${NC}"
    done

    # ── Summary ───────────────────────────────────────────────────────────────
    section "Summary — $AGENT"
    echo ""
    echo -e "  ${GREEN}✓ $PASS passed${NC}   ${RED}✗ $FAIL failed${NC}   ${YELLOW}! $WARN warnings${NC}"
    echo ""

done
