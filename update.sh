#!/bin/bash
# relay updater
# Updates all registered agents (or a specific one) from the release directory.
# Rebuilds the daemon binary once, then updates each agent's home.
# Preserves config, identity files, and data for every agent.
#
# Usage:
#   ./update.sh              # update all agents in ~/.relay
#   ./update.sh henry        # update only the 'henry' agent

set -e

RELEASE_DIR="$(cd "$(dirname "$0")" && pwd)"
TARGET_AGENT=""
WITH_MEMORY=0
WITHOUT_MEMORY=0
PURGE_DATA=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --with-memory)    WITH_MEMORY=1; shift ;;
        --without-memory) WITHOUT_MEMORY=1; shift ;;
        --purge-data)     PURGE_DATA=1; shift ;;
        --help|-h)
            echo "Usage: ./update.sh [AGENT] [--with-memory] [--without-memory [--purge-data]]"
            echo ""
            echo "  AGENT              Only update this agent (optional)"
            echo "  --with-memory      Install/refresh memory/FAISS system"
            echo "  --without-memory   Remove memory sidecar from existing install"
            echo "  --purge-data       With --without-memory: also delete data files"
            exit 0
            ;;
        *) TARGET_AGENT="$1"; shift ;;
    esac
done

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

# ── Banner ────────────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}  relay — updater${NC}"
echo ""

# ── Dependency check ──────────────────────────────────────────────────
command -v make >/dev/null 2>&1 || error "make not found. Install Xcode Command Line Tools: xcode-select --install"
command -v cc   >/dev/null 2>&1 || error "cc not found. Install Xcode Command Line Tools: xcode-select --install"

# ── Backward compat: migrate old single-agent install ─────────────────
# If no registry exists but ~/relay does, register it automatically.
REGISTRY="$HOME/.relay"

if [ ! -f "$REGISTRY" ] && [ -d "$HOME/relay" ]; then
    existing_name=$(grep "^agent_name" "$HOME/relay/config/relay.conf" 2>/dev/null \
        | cut -d= -f2 | tr -d ' ' | tr '[:upper:]' '[:lower:]')
    existing_name="${existing_name:-kai}"
    info "Migrating existing install to multi-agent registry..."
    echo "${existing_name}=$HOME/relay" >> "$REGISTRY"
    success "Registered ${existing_name}=$HOME/relay in ~/.relay"
fi

[ -f "$REGISTRY" ] || error "No agents registered. Run install.sh first."

# ── Shared paths ──────────────────────────────────────────────────────
SHARED_VENV="$HOME/.relay-shared/venv"
NEW_VERSION=$(git -C "$RELEASE_DIR" rev-parse --short=8 HEAD 2>/dev/null || echo "unknown")

# ── Build binary ONCE ─────────────────────────────────────────────────
info "Building daemon..."
make -C "$RELEASE_DIR/relay-daemon" clean --no-print-directory 2>/dev/null || true
make -C "$RELEASE_DIR/relay-daemon" --no-print-directory || error "Build failed"

DAEMON_BIN="$RELEASE_DIR/relay-daemon/relay"
[ -f "$DAEMON_BIN" ] || error "Build failed — binary not produced"
success "Build complete ($NEW_VERSION)"

# ── Create or update shared Python venv ──────────────────────────────
# Must exist before plists are regenerated — they reference its python3 binary.
if [ ! -d "$SHARED_VENV" ]; then
    info "Creating shared Python venv (~/.relay-shared/venv)..."
    mkdir -p "$HOME/.relay-shared"
    python3 -m venv "$SHARED_VENV" >/dev/null 2>&1
    success "Shared venv created"
fi

if [[ "$WITH_MEMORY" == "1" ]]; then
    info "Installing memory dependencies..."
    MEMORY_REQS="$RELEASE_DIR/apps/memory-py/requirements.txt"
    if [ ! -f "$MEMORY_REQS" ]; then
        error "Memory requirements not found at $MEMORY_REQS"
    fi
    "$SHARED_VENV/bin/pip" install -r "$MEMORY_REQS" -q 2>/dev/null || \
        error "Memory dependencies failed — run: $SHARED_VENV/bin/pip install -r $MEMORY_REQS"
    success "Memory dependencies installed"

    info "Checking embedding model..."
    BGE_CACHE_DIR="$HOME/.cache/huggingface/hub"
    if ls "$BGE_CACHE_DIR"/*bge-small-en* >/dev/null 2>&1; then
        success "BGE-small-en-v1.5 model already cached"
    else
        info "Downloading BGE-small-en-v1.5 (this may take a moment)..."
        "$SHARED_VENV/bin/python" -c \
            "from sentence_transformers import SentenceTransformer; SentenceTransformer('BAAI/bge-small-en-v1.5')" \
            2>&1 || error "Model download failed"
        success "BGE-small-en-v1.5 downloaded"
    fi
    success "Memory system ready."
else
    info "Updating shared Python dependencies..."
    "$SHARED_VENV/bin/pip" install \
        -r "$RELEASE_DIR/dashboard/requirements.txt" -q 2>/dev/null || true
    success "Shared venv up to date"
fi

# ── Dashboard update ───────────────────────────────────────────────────
DASHBOARD_DIR="$HOME/.relay-shared/dashboard"
DASHBOARD_LABEL="com.$(whoami).relay-dashboard"
DASHBOARD_PLIST="$HOME/Library/LaunchAgents/$DASHBOARD_LABEL.plist"

if [ -d "$DASHBOARD_DIR" ]; then
    info "Updating dashboard..."

    # Stop dashboard before replacing files
    dashboard_was_running=0
    if [[ "$OSTYPE" == "darwin"* ]] && [ -f "$DASHBOARD_PLIST" ]; then
        launchctl unload "$DASHBOARD_PLIST" 2>/dev/null && dashboard_was_running=1 || true
    fi

    # Copy server + static files
    mkdir -p "$DASHBOARD_DIR/static"
    cp "$RELEASE_DIR/dashboard/server.py"          "$DASHBOARD_DIR/server.py"
    cp "$RELEASE_DIR/dashboard/requirements.txt"   "$DASHBOARD_DIR/requirements.txt"
    cp "$RELEASE_DIR/dashboard/static/index.html"  "$DASHBOARD_DIR/static/index.html"
    cp "$RELEASE_DIR/dashboard/static/app.js"      "$DASHBOARD_DIR/static/app.js"
    cp "$RELEASE_DIR/dashboard/static/styles.css"  "$DASHBOARD_DIR/static/styles.css"

    # Regenerate dashboard LaunchAgent plist from template
    if [[ "$OSTYPE" == "darwin"* ]] && [ -f "$RELEASE_DIR/templates/config/dashboard.plist.template" ]; then
        path_val="/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin:$HOME/.local/bin"
        sed \
            -e "s|__DASHBOARD_LABEL__|$DASHBOARD_LABEL|g" \
            -e "s|__DASHBOARD_DIR__|$DASHBOARD_DIR|g" \
            -e "s|__SHARED_VENV__|$SHARED_VENV|g" \
            -e "s|__HOME__|$HOME|g" \
            -e "s|__PATH__|$path_val|g" \
            -e "s|__RELAY_RELEASE_DIR__|$RELEASE_DIR|g" \
            "$RELEASE_DIR/templates/config/dashboard.plist.template" > "$DASHBOARD_PLIST"
        dashboard_was_running=1
    fi

    # Restart dashboard
    if [ "$dashboard_was_running" -eq 1 ] && [ -f "$DASHBOARD_PLIST" ]; then
        launchctl load "$DASHBOARD_PLIST" 2>/dev/null && \
            success "Dashboard restarted" || \
            warn "Dashboard restart failed — run: launchctl load $DASHBOARD_PLIST"
    fi

    success "Dashboard updated"
fi

# ── Per-agent update function ─────────────────────────────────────────
update_agent() {
    local agent_name="$1"
    local agent_home="${2/#\~/$HOME}"  # expand tilde

    echo ""
    info "Updating agent: $agent_name ($agent_home)"

    [ -d "$agent_home" ] || { warn "Home dir not found: $agent_home — skipping $agent_name"; return; }
    [ -f "$agent_home/bin/relay" ] || { warn "No binary in $agent_home/bin — skipping $agent_name"; return; }

    local installed
    installed=$(cat "$agent_home/config/.version" 2>/dev/null || echo "unknown")
    info "  Installed: $installed  →  This version: $NEW_VERSION"

    # Read pid_file from agent's relay.conf (handles legacy /tmp/relay.pid and new /tmp/relay-{name}.pid)
    local pid_file
    pid_file=$(grep "^pid_file" "$agent_home/config/relay.conf" 2>/dev/null \
        | cut -d= -f2 | tr -d ' ')
    pid_file="${pid_file:-/tmp/relay.pid}"
    pid_file="${pid_file/#\~/$HOME}"

    # Determine plist label — new agents use com.user.relay-{name}, legacy use com.user.relay
    local base_label="com.$(whoami)"
    local plist_label
    if [ -f "$HOME/Library/LaunchAgents/$base_label.relay-$agent_name.plist" ]; then
        plist_label="$base_label.relay-$agent_name"
    else
        plist_label="$base_label.relay"  # legacy single-agent naming
    fi

    local plist_path="$HOME/Library/LaunchAgents/$plist_label.plist"
    local mi_plist_path="$HOME/Library/LaunchAgents/$plist_label.memory-index.plist"
    local st_plist_path="$HOME/Library/LaunchAgents/$plist_label.state-tracker.plist"
    local daemon_was_running=0
    local mi_was_running=0
    local st_was_running=0

    # ── Patch relay.conf before stopping — avoids race where watchdog respawns
    #    relay before we finish patching ──
    if [[ "$WITH_MEMORY" == "1" ]]; then
        local conf="$agent_home/config/relay.conf"
        if [ -f "$conf" ]; then
            if grep -q "^memory_service_autostart" "$conf"; then
                sed -i '' 's/^memory_service_autostart = .*/memory_service_autostart = 1/' "$conf" 2>/dev/null || \
                sed -i    's/^memory_service_autostart = .*/memory_service_autostart = 1/' "$conf"
            else
                # Insert before first [workspace line — keys after a [workspace]
                # section header are silently ignored by the config parser
                awk '/^\[workspace/{if(!p){print "memory_service_autostart = 1"; p=1}} 1' \
                    "$conf" > "$conf.tmp" && mv "$conf.tmp" "$conf" || \
                    echo "memory_service_autostart = 1" >> "$conf"
            fi
            if ! grep -q "^memory_service_script" "$conf"; then
                local script_val="$agent_home/apps/memory-py/start-memory.sh"
                awk -v val="memory_service_script = $script_val" \
                    '/^\[workspace/{if(!p){print val; p=1}} 1' \
                    "$conf" > "$conf.tmp" && mv "$conf.tmp" "$conf" || \
                    echo "memory_service_script = $script_val" >> "$conf"
            else
                # Update existing entry to use wrapper script if pointing at .py
                sed -i '' 's|memory_service_script = .*/memory_http\.py|memory_service_script = '"$agent_home/apps/memory-py/start-memory.sh"'|' "$conf" 2>/dev/null || \
                sed -i    's|memory_service_script = .*/memory_http\.py|memory_service_script = '"$agent_home/apps/memory-py/start-memory.sh"'|' "$conf"
            fi
            success "  memory_service_autostart = 1 set in relay.conf"
        else
            warn "  relay.conf not found at $conf — skipping autostart patch"
        fi
    fi

    # ── Stop daemons ──
    # Check if daemon is actually running before stopping
    local pid
    pid=$(cat "$pid_file" 2>/dev/null)
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        daemon_was_running=1
    fi

    if [[ "$OSTYPE" == "darwin"* ]] && [ -f "$plist_path" ]; then
        info "  Stopping daemon (LaunchAgent)..."
        launchctl unload "$plist_path" 2>/dev/null && daemon_was_running=1 || true
    fi

    # Kill the relay process directly — watchdog starts it separately from the LaunchAgent
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        info "  Stopping relay process (PID $pid)..."
        kill -TERM "$pid" 2>/dev/null || true
        local i=0
        while kill -0 "$pid" 2>/dev/null && [ $i -lt 5 ]; do
            sleep 1; i=$((i+1))
        done
    fi

    if [[ "$OSTYPE" == "darwin"* ]] && [ -f "$mi_plist_path" ]; then
        launchctl unload "$mi_plist_path" 2>/dev/null && mi_was_running=1 || true
    fi

    if [[ "$OSTYPE" == "darwin"* ]] && [ -f "$st_plist_path" ]; then
        launchctl unload "$st_plist_path" 2>/dev/null && st_was_running=1 || true
    fi

    # ── Ensure dir structure is current ──
    mkdir -p "$agent_home"/data/schedule
    mkdir -p "$agent_home"/scripts/maintenance

    # ── Install updated files (preserve user files) ──
    info "  Installing update..."

    # Binary
    cp "$DAEMON_BIN" "$agent_home/bin/relay"
    chmod +x "$agent_home/bin/relay"
    if [[ "$OSTYPE" == "darwin"* ]]; then
        codesign -s - "$agent_home/bin/relay" 2>/dev/null || true
    fi

    # Memory sidecar scripts (update if already installed or --with-memory)
    if [[ "$WITH_MEMORY" == "1" ]] || [ -d "$agent_home/apps/memory-py" ]; then
        if ls "$RELEASE_DIR"/apps/memory-py/*.py >/dev/null 2>&1; then
            mkdir -p "$agent_home/apps/memory-py"
            cp "$RELEASE_DIR"/apps/memory-py/*.py "$agent_home/apps/memory-py/"
            cp "$RELEASE_DIR/apps/memory-py/requirements.txt" "$agent_home/apps/memory-py/"
            chmod +x "$agent_home/apps/memory-py/memory_http.py" 2>/dev/null || true
        fi
        if [ -f "$RELEASE_DIR/apps/memory-py/start-memory.sh" ]; then
            cp "$RELEASE_DIR/apps/memory-py/start-memory.sh" "$agent_home/apps/memory-py/"
            chmod +x "$agent_home/apps/memory-py/start-memory.sh"
        fi
    fi

    # Maintenance scripts
    if ls "$RELEASE_DIR"/scripts/maintenance/*.sh >/dev/null 2>&1; then
        cp "$RELEASE_DIR"/scripts/maintenance/*.sh "$agent_home/scripts/maintenance/"
        chmod +x "$agent_home"/scripts/maintenance/*.sh
    fi

    # Doctor script
    cp "$RELEASE_DIR/scripts/doctor.sh" "$agent_home/bin/relay-doctor"
    chmod +x "$agent_home/bin/relay-doctor"

    # Non-secret configs (NOT relay.conf — that has user secrets)
    [ -f "$RELEASE_DIR/templates/config/llm_format.conf" ] && \
        cp "$RELEASE_DIR/templates/config/llm_format.conf" "$agent_home/config/"
    [ -f "$RELEASE_DIR/templates/config/relay.plist.template" ] && \
        cp "$RELEASE_DIR/templates/config/relay.plist.template" "$agent_home/config/"
    [ -f "$RELEASE_DIR/templates/config/memory_index.plist.template" ] && \
        cp "$RELEASE_DIR/templates/config/memory_index.plist.template" "$agent_home/config/"
    [ -f "$RELEASE_DIR/templates/config/state_tracker.plist.template" ] && \
        cp "$RELEASE_DIR/templates/config/state_tracker.plist.template" "$agent_home/config/"

    # Seed optional JSON configs if not already customised by user
    [ -f "$agent_home/config/deadlines.json" ] || \
        [ ! -f "$RELEASE_DIR/templates/config/deadlines.json" ] || \
        cp "$RELEASE_DIR/templates/config/deadlines.json" "$agent_home/config/deadlines.json"
    [ -f "$agent_home/config/monitor_sites.json" ] || \
        [ ! -f "$RELEASE_DIR/templates/config/monitor_sites.json" ] || \
        cp "$RELEASE_DIR/templates/config/monitor_sites.json" "$agent_home/config/monitor_sites.json"

    # Skills (additive — preserves user-added skills, updates bundled ones)
    if ls "$RELEASE_DIR"/skills/ >/dev/null 2>&1; then
        mkdir -p "$agent_home/skills"
        cp "$RELEASE_DIR"/skills/*.sh "$agent_home/skills/" 2>/dev/null || true
        cp "$RELEASE_DIR"/skills/*.md "$agent_home/skills/" 2>/dev/null || true
        chmod +x "$agent_home"/skills/*.sh 2>/dev/null || true
    fi

    # Skills manifest — generated dynamically by scanning skill dependencies
    if [ -x "$RELEASE_DIR/scripts/skill-manifest.sh" ]; then
        "$RELEASE_DIR/scripts/skill-manifest.sh" "$agent_home" "$agent_home/SKILLS.md" >/dev/null 2>&1 || \
            { [ -f "$RELEASE_DIR/templates/SKILLS.md" ] && \
              cp "$RELEASE_DIR/templates/SKILLS.md" "$agent_home/SKILLS.md"; }
    elif [ -f "$RELEASE_DIR/templates/SKILLS.md" ]; then
        cp "$RELEASE_DIR/templates/SKILLS.md" "$agent_home/SKILLS.md"
    fi

    # Regenerate sub-daemon LaunchAgent plists from updated templates.
    # Always regenerate (daemons stopped above) so env var changes take effect.
    if [[ "$OSTYPE" == "darwin"* ]] && [ -f "$plist_path" ]; then
        local path_val="/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin:$HOME/.local/bin"

        if [ -f "$agent_home/config/memory_index.plist.template" ]; then
            info "  Updating Memory Index LaunchAgent..."
            sed \
                -e "s|__RELAY_HOME__|$agent_home|g" \
                -e "s|__PLIST_LABEL__|$plist_label|g" \
                -e "s|__PATH__|$path_val|g" \
                -e "s|__SHARED_VENV__|$SHARED_VENV|g" \
                "$agent_home/config/memory_index.plist.template" > "$mi_plist_path"
            mi_was_running=1
        fi

        if [ -f "$agent_home/config/state_tracker.plist.template" ]; then
            info "  Updating State Tracker LaunchAgent..."
            sed \
                -e "s|__RELAY_HOME__|$agent_home|g" \
                -e "s|__PLIST_LABEL__|$plist_label|g" \
                -e "s|__PATH__|$path_val|g" \
                -e "s|__SHARED_VENV__|$SHARED_VENV|g" \
                "$agent_home/config/state_tracker.plist.template" > "$st_plist_path"
            st_was_running=1
        fi
    fi

    # Save new version
    echo "$NEW_VERSION" > "$agent_home/config/.version"

    # ── Restart ──
    if [[ "$OSTYPE" == "darwin"* ]] && [ -f "$plist_path" ]; then
        info "  Restarting $agent_name daemon..."
        launchctl load "$plist_path" 2>/dev/null && \
            success "  $agent_name daemon restarted" || \
            warn "  Restart failed — run: launchctl load $plist_path"
    elif [ "$daemon_was_running" -eq 1 ]; then
        nohup "$agent_home/bin/relay" start -c "$agent_home/config/relay.conf" \
            >> "$agent_home/logs/relay.log" 2>&1 &
        success "  $agent_name daemon restarted (PID $!)"
    fi

    if [ "$mi_was_running" -eq 1 ] && [ -f "$mi_plist_path" ]; then
        launchctl load "$mi_plist_path" 2>/dev/null && \
            success "  $agent_name Memory Index restarted" || \
            warn "  Restart failed — run: launchctl load $mi_plist_path"
    fi

    if [ "$st_was_running" -eq 1 ] && [ -f "$st_plist_path" ]; then
        launchctl load "$st_plist_path" 2>/dev/null && \
            success "  $agent_name State Tracker restarted" || \
            warn "  Restart failed — run: launchctl load $st_plist_path"
    fi

    success "Agent $agent_name updated to $NEW_VERSION"
}

# ── Remove memory from an agent (--without-memory) ────────────────────
remove_memory_from_agent() {
    local agent_name="$1"
    local agent_home="${2/#\~/$HOME}"
    local conf="$agent_home/config/relay.conf"

    echo ""
    echo -e "${BOLD}━━━ Removing memory: $agent_name @ $agent_home ━━━${NC}"

    # Check if memory was installed
    local has_venv=0
    local has_proc=0
    [ -d "$SHARED_VENV" ] && has_venv=1
    pgrep -f "memory_http.py.*$agent_home" >/dev/null 2>&1 && has_proc=1

    if [ "$has_venv" -eq 0 ] && [ "$has_proc" -eq 0 ] && \
       [ ! -d "$agent_home/apps/memory-py" ]; then
        info "Memory system was not installed — nothing to remove."
        return
    fi

    # Stop running sidecar
    if pgrep -f "memory_http.py.*$agent_home" >/dev/null 2>&1; then
        info "Stopping memory sidecar..."
        pkill -TERM -f "memory_http.py.*$agent_home" 2>/dev/null || true
        local i=0
        while pgrep -f "memory_http.py.*$agent_home" >/dev/null 2>&1 && [ $i -lt 5 ]; do
            sleep 1; i=$((i+1))
        done
        if pgrep -f "memory_http.py.*$agent_home" >/dev/null 2>&1; then
            pkill -KILL -f "memory_http.py.*$agent_home" 2>/dev/null || true
        fi
        success "Memory sidecar stopped"
    else
        info "Memory sidecar not running"
    fi

    # Remove Python venv
    if [ -d "$SHARED_VENV" ]; then
        info "Removing memory venv..."
        rm -rf "$SHARED_VENV"
        success "Venv removed ($SHARED_VENV)"
    fi

    # Disable autostart in config
    if [ -f "$conf" ]; then
        sed -i '' 's/^memory_service_autostart = .*/memory_service_autostart = 0/' "$conf" 2>/dev/null || \
        sed -i    's/^memory_service_autostart = .*/memory_service_autostart = 0/' "$conf" 2>/dev/null || true
        success "memory_service_autostart = 0 written to relay.conf"
    else
        warn "relay.conf not found at $conf — config not updated"
    fi

    # Optionally purge data
    if [[ "$PURGE_DATA" == "1" ]]; then
        echo ""
        echo -e "${RED}This will permanently delete memory data for $agent_name:${NC}"
        echo "  $agent_home/data/memory/.index/"
        echo "  $agent_home/data/memory/.ingest_cursor.json"
        echo "  $agent_home/data/memory/review_queue.jsonl"
        echo ""
        echo -n "  Are you sure? [y/N]: "
        read -r CONFIRM_PURGE
        if [[ "$CONFIRM_PURGE" == "y" || "$CONFIRM_PURGE" == "Y" ]]; then
            rm -rf "$agent_home/data/memory/.index" 2>/dev/null || true
            rm -f  "$agent_home/data/memory/.ingest_cursor.json" 2>/dev/null || true
            rm -f  "$agent_home/data/memory/review_queue.jsonl" 2>/dev/null || true
            success "Data purged."
        else
            info "Data preserved at $agent_home/data/memory/"
        fi
    else
        info "Memory system disabled. Data preserved at $agent_home/data/memory/"
    fi
}

# ── Loop over registry ────────────────────────────────────────────────
UPDATED=0

while IFS='=' read -r agent_name agent_home; do
    # Skip comments and blank lines
    [[ "$agent_name" =~ ^[[:space:]]*# ]] && continue
    [[ -z "$agent_name" ]] && continue

    # If a specific agent was requested, skip others
    if [ -n "$TARGET_AGENT" ] && [ "$agent_name" != "$TARGET_AGENT" ]; then
        continue
    fi

    if [[ "$WITHOUT_MEMORY" == "1" ]]; then
        remove_memory_from_agent "$agent_name" "$agent_home"
    else
        update_agent "$agent_name" "$agent_home"
    fi
    UPDATED=$((UPDATED + 1))
done < "$REGISTRY"

# ── Verify something was updated ──────────────────────────────────────
if [ "$UPDATED" -eq 0 ]; then
    if [ -n "$TARGET_AGENT" ]; then
        error "Agent '$TARGET_AGENT' not found in ~/.relay"
    else
        error "No agents found in ~/.relay"
    fi
fi

# ── Done ──────────────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}${BOLD}✅ Updated $UPDATED agent(s) to $NEW_VERSION${NC}"
echo ""
echo "  Your config, identity files, and data were not modified."
echo ""

if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "  Service status:"
    while IFS='=' read -r agent_name agent_home; do
        [[ "$agent_name" =~ ^[[:space:]]*# ]] && continue
        [[ -z "$agent_name" ]] && continue
        if [ -n "$TARGET_AGENT" ] && [ "$agent_name" != "$TARGET_AGENT" ]; then
            continue
        fi
        base_label="com.$(whoami)"
        if [ -f "$HOME/Library/LaunchAgents/$base_label.relay-$agent_name.plist" ]; then
            plist_label="$base_label.relay-$agent_name"
        else
            plist_label="$base_label.relay"
        fi
        agent_home_expanded="${agent_home/#\~/$HOME}"
        # Main relay daemon status via launchctl
        pid=$(launchctl list 2>/dev/null | awk -v lbl="$plist_label" '$3 == lbl {print $1}')
        if [ -n "$pid" ] && [ "$pid" != "-" ]; then
            echo -e "  ${GREEN}✓${NC} $plist_label (PID $pid)"
        else
            echo -e "  ${YELLOW}!${NC} $plist_label (not running)"
        fi
        # Memory sidecar status via pgrep (only if installed)
        if [ -f "$agent_home_expanded/apps/memory-py/memory_http.py" ]; then
            sidecar_pid=$(pgrep -f "memory_http.py.*$agent_home_expanded" 2>/dev/null | head -1)
            if [ -n "$sidecar_pid" ]; then
                echo -e "  ${GREEN}✓${NC} memory sidecar (PID $sidecar_pid)"
            else
                echo -e "  ${YELLOW}!${NC} memory sidecar (not running)"
            fi
        fi
    done < "$REGISTRY"
    echo ""
fi
