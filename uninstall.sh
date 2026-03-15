#!/bin/bash
# relay uninstaller — removes registered agents using the ~/.relay registry
#
# Usage:
#   ./uninstall.sh              # list agents, choose which to remove
#   ./uninstall.sh kai          # remove only the 'kai' agent
#   ./uninstall.sh --all        # remove all registered agents
#   ./uninstall.sh --help       # show usage

set -e

# ── Colors ────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m'

info()    { echo -e "${BLUE}==>${NC} $1"; }
success() { echo -e "${GREEN}✓${NC} $1"; }
warn()    { echo -e "${YELLOW}!${NC} $1"; }
error()   { echo -e "${RED}✗${NC} $1"; exit 1; }

# ── Parse arguments ──────────────────────────────────────────────────
TARGET_AGENT=""
REMOVE_ALL=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --all)     REMOVE_ALL=1; shift ;;
        --help|-h)
            echo "Usage: ./uninstall.sh [AGENT] [--all]"
            echo ""
            echo "  AGENT    Remove only this agent (optional)"
            echo "  --all    Remove all registered agents without prompting for selection"
            echo ""
            echo "If no argument is given, lists registered agents and asks which to remove."
            exit 0
            ;;
        *) TARGET_AGENT="$1"; shift ;;
    esac
done

# ── Banner ────────────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}  relay — uninstaller${NC}"
echo ""

# ── Registry check ───────────────────────────────────────────────────
REGISTRY="$HOME/.relay"

if [ ! -f "$REGISTRY" ]; then
    error "No agents registered (~/.relay not found). Nothing to uninstall."
fi

# Count agents
AGENT_COUNT=0
while IFS='=' read -r name home; do
    [[ "$name" =~ ^[[:space:]]*# ]] && continue
    [[ -z "$name" ]] && continue
    AGENT_COUNT=$((AGENT_COUNT + 1))
done < "$REGISTRY"

if [ "$AGENT_COUNT" -eq 0 ]; then
    error "No agents registered in ~/.relay. Nothing to uninstall."
fi

# ── Resolve which agent(s) to remove ────────────────────────────────
if [ -n "$TARGET_AGENT" ]; then
    # Verify the agent exists in the registry
    if ! grep -q "^${TARGET_AGENT}=" "$REGISTRY" 2>/dev/null; then
        error "Agent '$TARGET_AGENT' not found in ~/.relay"
    fi
elif [ "$REMOVE_ALL" -eq 0 ]; then
    # Interactive: list agents and ask
    echo "  Registered agents:"
    echo ""
    while IFS='=' read -r name home; do
        [[ "$name" =~ ^[[:space:]]*# ]] && continue
        [[ -z "$name" ]] && continue
        echo "    $name → $home"
    done < "$REGISTRY"
    echo ""
    read -r -p "  Agent to remove (or 'all'): " SELECTION
    if [ -z "$SELECTION" ]; then
        echo "Aborted."
        exit 0
    elif [ "$SELECTION" = "all" ]; then
        REMOVE_ALL=1
    else
        TARGET_AGENT="$SELECTION"
        if ! grep -q "^${TARGET_AGENT}=" "$REGISTRY" 2>/dev/null; then
            error "Agent '$TARGET_AGENT' not found in ~/.relay"
        fi
    fi
fi

# ── Build list of what will be removed ───────────────────────────────
echo ""
echo -e "${BOLD}This will remove:${NC}"
echo ""

AGENTS_TO_REMOVE=()
while IFS='=' read -r name home; do
    [[ "$name" =~ ^[[:space:]]*# ]] && continue
    [[ -z "$name" ]] && continue
    if [ "$REMOVE_ALL" -eq 1 ] || [ "$name" = "$TARGET_AGENT" ]; then
        home="${home/#\~/$HOME}"
        echo "  - $name ($home)"
        AGENTS_TO_REMOVE+=("$name=$home")
    fi
done < "$REGISTRY"

echo ""
read -r -p "Are you sure? [y/N]: " CONFIRM
if [[ "$CONFIRM" != "y" && "$CONFIRM" != "Y" ]]; then
    echo "Aborted."
    exit 0
fi

# ── Uninstall function (per agent) ───────────────────────────────────
uninstall_agent() {
    local agent_name="$1"
    local agent_home="$2"

    echo ""
    info "Removing agent: $agent_name ($agent_home)"

    # ── Determine plist label (multi-agent vs legacy) ──
    local base_label="com.$(whoami)"
    local plist_label
    if [ -f "$HOME/Library/LaunchAgents/$base_label.relay-$agent_name.plist" ]; then
        plist_label="$base_label.relay-$agent_name"
    else
        plist_label="$base_label.relay"
    fi

    # ── Read PID file path from relay.conf ──
    local pid_file
    if [ -f "$agent_home/config/relay.conf" ]; then
        pid_file=$(grep "^pid_file" "$agent_home/config/relay.conf" 2>/dev/null \
            | cut -d= -f2 | tr -d ' ')
    fi
    pid_file="${pid_file:-/tmp/relay-${agent_name}.pid}"
    pid_file="${pid_file/#\~/$HOME}"

    # Also check legacy PID file location
    local legacy_pid_file="/tmp/relay.pid"

    # ── Stop daemon ──
    for pf in "$pid_file" "$legacy_pid_file"; do
        if [ -f "$pf" ]; then
            local pid
            pid=$(cat "$pf" 2>/dev/null)
            if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
                info "  Stopping daemon (PID $pid)..."
                kill -TERM "$pid" 2>/dev/null || true
                local i=0
                while kill -0 "$pid" 2>/dev/null && [ $i -lt 5 ]; do
                    sleep 1; i=$((i+1))
                done
            fi
            rm -f "$pf"
        fi
    done

    # ── Unload LaunchAgent ──
    local plist_path="$HOME/Library/LaunchAgents/$plist_label.plist"
    if [ -f "$plist_path" ]; then
        info "  Unloading LaunchAgent ($plist_label)..."
        launchctl unload "$plist_path" 2>/dev/null || true
        rm -f "$plist_path"
    fi

    # ── Unload sub-daemon plists (memory-index, state-tracker) ──
    for suffix in "memory-index" "state-tracker"; do
        local sub_plist="$HOME/Library/LaunchAgents/$plist_label.$suffix.plist"
        if [ -f "$sub_plist" ]; then
            info "  Unloading $suffix LaunchAgent..."
            launchctl unload "$sub_plist" 2>/dev/null || true
            rm -f "$sub_plist"
        fi
    done

    # ── Remove agent home ──
    if [ -d "$agent_home" ]; then
        info "  Removing $agent_home..."
        rm -rf "$agent_home"
    fi

    # ── Remove entry from registry ──
    if [ -f "$REGISTRY" ]; then
        grep -v "^${agent_name}=" "$REGISTRY" > "$REGISTRY.tmp" 2>/dev/null || true
        mv "$REGISTRY.tmp" "$REGISTRY"
    fi

    success "Agent $agent_name removed"
}

# ── Run uninstall for each agent ─────────────────────────────────────
REMOVED=0
for entry in "${AGENTS_TO_REMOVE[@]}"; do
    agent_name="${entry%%=*}"
    agent_home="${entry#*=}"
    uninstall_agent "$agent_name" "$agent_home"
    REMOVED=$((REMOVED + 1))
done

# ── Clean up dashboard if no agents remain ───────────────────────────
REMAINING=0
if [ -f "$REGISTRY" ]; then
    while IFS='=' read -r name home; do
        [[ "$name" =~ ^[[:space:]]*# ]] && continue
        [[ -z "$name" ]] && continue
        REMAINING=$((REMAINING + 1))
    done < "$REGISTRY"
fi

if [ "$REMAINING" -eq 0 ]; then
    # Remove dashboard
    DASHBOARD_LABEL="com.$(whoami).relay-dashboard"
    DASHBOARD_PLIST="$HOME/Library/LaunchAgents/$DASHBOARD_LABEL.plist"
    DASHBOARD_DIR="$HOME/.relay-shared/dashboard"

    if [ -f "$DASHBOARD_PLIST" ]; then
        info "No agents remaining — removing dashboard LaunchAgent..."
        launchctl unload "$DASHBOARD_PLIST" 2>/dev/null || true
        rm -f "$DASHBOARD_PLIST"
    fi

    if [ -d "$DASHBOARD_DIR" ]; then
        info "Removing dashboard files..."
        rm -rf "$DASHBOARD_DIR"
    fi

    # Remove shared venv
    if [ -d "$HOME/.relay-shared" ]; then
        info "Removing shared resources (~/.relay-shared)..."
        rm -rf "$HOME/.relay-shared"
    fi

    # Remove empty registry
    if [ -f "$REGISTRY" ] && [ ! -s "$REGISTRY" ]; then
        rm -f "$REGISTRY"
    fi

    success "Dashboard and shared resources removed"
fi

# ── Done ─────────────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}${BOLD}Removed $REMOVED agent(s).${NC}"
echo ""
