#!/bin/bash
# relay installer
# Installs the relay daemon with a clean directory structure.
# No ML dependencies by default. Use --with-memory to opt in.
#
# Usage:
#   ./install.sh                          # interactive prompts
#   ./install.sh --name henry --home ~/henry
#   ./install.sh --with-memory            # include memory/FAISS system

set -e

RELEASE_DIR="$(cd "$(dirname "$0")" && pwd)"

# ── Parse arguments ───────────────────────────────────────────────────
RELAY_NAME=""
RELAY_HOME=""
WITH_MEMORY=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --name)        RELAY_NAME="$2"; shift 2 ;;
        --home)        RELAY_HOME="$2"; shift 2 ;;
        --with-memory) WITH_MEMORY=1; shift ;;
        --help|-h)
            echo "Usage: ./install.sh [--name NAME] [--home PATH] [--with-memory]"
            echo ""
            echo "  --name NAME        Agent slug (e.g. kai, nova)"
            echo "  --home PATH        Install directory (default: ~/NAME)"
            echo "  --with-memory      Install memory/FAISS system (optional, heavy deps)"
            exit 0
            ;;
        *) shift ;;
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
echo -e "${BOLD}  relay — AI Agent Daemon${NC}"
echo "  github.com/rawphp/relay"
echo ""

# ── Dependency checks ─────────────────────────────────────────────────
info "Checking dependencies..."

command -v make >/dev/null 2>&1 || error "make not found. Install Xcode Command Line Tools: xcode-select --install"
command -v cc   >/dev/null 2>&1 || error "cc not found. Install Xcode Command Line Tools: xcode-select --install"
command -v curl >/dev/null 2>&1 || error "curl not found."

if [[ "$WITH_MEMORY" == "1" ]]; then
    command -v python3 >/dev/null 2>&1 || error "python3 not found (required for --with-memory). brew install python3"
fi

success "Dependencies OK"

# ── Resolve install name and home ─────────────────────────────────────
echo ""
echo -e "${BOLD}Setup — press Enter to accept defaults${NC}"
echo ""

if [ -z "$RELAY_NAME" ]; then
    read -r -p "  Agent name (slug, e.g. kai, nova) [kai]: " RELAY_NAME
    RELAY_NAME="${RELAY_NAME:-kai}"
fi

# Sanitize: lowercase, spaces to hyphens
RELAY_NAME=$(echo "$RELAY_NAME" | tr '[:upper:]' '[:lower:]' | tr ' ' '-')

# ── Check for existing registration ───────────────────────────────────
REGISTRY="$HOME/.relay"
if [ -f "$REGISTRY" ] && grep -q "^${RELAY_NAME}=" "$REGISTRY" 2>/dev/null; then
    EXISTING_HOME=$(grep "^${RELAY_NAME}=" "$REGISTRY" | cut -d= -f2-)
    echo -e "${RED}✗${NC} '${RELAY_NAME}' is already installed at ${EXISTING_HOME}."
    echo ""
    echo "  To update to the latest version, run:"
    echo "    ./update.sh"
    echo ""
    exit 1
fi

if [ -n "$RELAY_HOME" ]; then
    RELAY_HOME="${RELAY_HOME/#\~/$HOME}"
else
    DEFAULT_HOME="$HOME/$RELAY_NAME"
    read -r -p "  Install directory [$DEFAULT_HOME]: " RELAY_HOME
    RELAY_HOME="${RELAY_HOME:-$DEFAULT_HOME}"
    RELAY_HOME="${RELAY_HOME/#\~/$HOME}"
fi

# ── Already installed? ────────────────────────────────────────────────
if [ -d "$RELAY_HOME" ]; then
    warn "$RELAY_HOME already exists."
    echo -n "  Reinstall? This will overwrite config and binary [y/N]: "
    read -r REINSTALL
    if [[ "$REINSTALL" != "y" && "$REINSTALL" != "Y" ]]; then
        echo "Aborted."
        exit 0
    fi
fi

# ── Agent identity ────────────────────────────────────────────────────
DEFAULT_DISPLAY=$(echo "$RELAY_NAME" | awk '{print toupper(substr($0,1,1)) substr($0,2)}')

read -r -p "  Agent display name [$DEFAULT_DISPLAY]: " AGENT_NAME
AGENT_NAME="${AGENT_NAME:-$DEFAULT_DISPLAY}"

read -r -p "  Your name:          " USER_NAME
USER_NAME="${USER_NAME:-User}"

# ── Telegram credentials ──────────────────────────────────────────────
echo ""
echo "  You need a Telegram bot token from @BotFather"
echo "  and your Telegram user ID from @userinfobot."
echo ""
read -r -p "  Telegram bot token: " TELEGRAM_TOKEN
read -r -p "  Telegram user ID:   " TELEGRAM_USER_ID

if [ -z "$TELEGRAM_TOKEN" ] || [ -z "$TELEGRAM_USER_ID" ]; then
    warn "No Telegram credentials — add them to $RELAY_HOME/config/relay.conf later."
    TELEGRAM_TOKEN="YOUR_BOT_TOKEN_FROM_BOTFATHER"
    TELEGRAM_USER_ID="YOUR_TELEGRAM_USER_ID"
fi

# ── Workspace setup ───────────────────────────────────────────────────
echo ""
echo "  Workspaces are directories you want the agent to work in."
echo "  You can add more later by editing relay.conf."
echo ""
read -r -p "  Workspace name (e.g. myapp, work, personal) [main]: " WS_NAME
WS_NAME="${WS_NAME:-main}"

read -r -p "  Workspace path (e.g. ~/Code/myapp) [$HOME/$WS_NAME]: " WS_PATH
WS_PATH="${WS_PATH:-$HOME/$WS_NAME}"
WS_PATH="${WS_PATH/#\~/$HOME}"

read -r -p "  LLM provider for this workspace (claude/codex/gemini) [claude]: " WS_PROVIDER
WS_PROVIDER="${WS_PROVIDER:-claude}"

# ── Create directory structure ────────────────────────────────────────
echo ""
info "Creating $RELAY_HOME..."

mkdir -p "$RELAY_HOME"/{bin,config,logs}
mkdir -p "$RELAY_HOME"/data/{memory,transcripts,.telegram-photos}

success "Directory structure created"

# ── Build daemon ──────────────────────────────────────────────────────
info "Building relay daemon..."

make -C "$RELEASE_DIR/relay-daemon" --no-print-directory 2>&1 | \
    grep -E "^(cc|error:|warning:)" | head -20 || true

DAEMON_BIN="$RELEASE_DIR/relay-daemon/relay"
if [ ! -f "$DAEMON_BIN" ]; then
    error "Build failed — no binary at $DAEMON_BIN"
fi

cp "$DAEMON_BIN" "$RELAY_HOME/bin/relay"
chmod +x "$RELAY_HOME/bin/relay"
success "Daemon built → $RELAY_HOME/bin/relay"

# ── Optional: memory system ───────────────────────────────────────────
MEMORY_ENABLED=0
MEMORY_SERVICE_AUTOSTART=0

if [[ "$WITH_MEMORY" == "1" ]]; then
    info "Installing memory dependencies..."

    SHARED_VENV="$HOME/.relay-shared/venv"
    if [ ! -d "$SHARED_VENV" ]; then
        mkdir -p "$HOME/.relay-shared"
        python3 -m venv "$SHARED_VENV" >/dev/null 2>&1
        success "Shared venv created (~/.relay-shared/venv)"
    fi

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

    # Copy memory sidecar into agent home
    mkdir -p "$RELAY_HOME/apps/memory-py"
    cp "$RELEASE_DIR"/apps/memory-py/*.py "$RELAY_HOME/apps/memory-py/"
    cp "$RELEASE_DIR/apps/memory-py/requirements.txt" "$RELAY_HOME/apps/memory-py/"
    chmod +x "$RELAY_HOME/apps/memory-py/memory_http.py" 2>/dev/null || true

    MEMORY_ENABLED=1
    MEMORY_SERVICE_AUTOSTART=1
    success "Memory system ready."
fi

# ── Generate config ───────────────────────────────────────────────────
info "Generating config..."

MEMORY_LINE="memory_search_enabled = $MEMORY_ENABLED"

sed \
    -e "s|{{AGENT_NAME}}|$AGENT_NAME|g" \
    -e "s|{{RELAY_NAME}}|$RELAY_NAME|g" \
    -e "s|{{USER_NAME}}|$USER_NAME|g" \
    -e "s|{{TELEGRAM_TOKEN}}|$TELEGRAM_TOKEN|g" \
    -e "s|{{TELEGRAM_USER_ID}}|$TELEGRAM_USER_ID|g" \
    -e "s|{{RELAY_HOME}}|$RELAY_HOME|g" \
    -e "s|{{MEMORY_SEARCH_ENABLED}}|$MEMORY_ENABLED|g" \
    -e "s|{{MEMORY_SERVICE_AUTOSTART}}|$MEMORY_SERVICE_AUTOSTART|g" \
    -e "s|{{WORKSPACE_NAME}}|$WS_NAME|g" \
    -e "s|{{WORKSPACE_PATH}}|$WS_PATH|g" \
    -e "s|{{WORKSPACE_PROVIDER}}|$WS_PROVIDER|g" \
    "$RELEASE_DIR/templates/config/relay.conf.template" \
    > "$RELAY_HOME/config/relay.conf"

chmod 600 "$RELAY_HOME/config/relay.conf"
success "Config written → $RELAY_HOME/config/relay.conf (chmod 600)"

# ── Generate identity files ───────────────────────────────────────────
info "Generating identity files..."

sub() {
    sed \
        -e "s|{{AGENT_NAME}}|$AGENT_NAME|g" \
        -e "s|{{USER_NAME}}|$USER_NAME|g" \
        -e "s|{{RELAY_HOME}}|$RELAY_HOME|g" \
        -e "s|{{WORKSPACE_NAME}}|$WS_NAME|g" \
        -e "s|{{WORKSPACE_PATH}}|$WS_PATH|g" \
        -e "s|{{WORKSPACE_PROVIDER}}|$WS_PROVIDER|g" \
        "$1" > "$2"
}

sub "$RELEASE_DIR/templates/CLAUDE.md.template"    "$RELAY_HOME/CLAUDE.md"
sub "$RELEASE_DIR/templates/IDENTITY.md.template"  "$RELAY_HOME/IDENTITY.md"
sub "$RELEASE_DIR/templates/USER.md.template"      "$RELAY_HOME/USER.md"

cp "$RELEASE_DIR/templates/SOUL.md"       "$RELAY_HOME/SOUL.md"
cp "$RELEASE_DIR/templates/PRIORITIES.md" "$RELAY_HOME/PRIORITIES.md"
cp "$RELEASE_DIR/templates/MEMORY.md"     "$RELAY_HOME/MEMORY.md"
# SKILLS.md — generated dynamically by scanning skill dependencies
if [ -x "$RELEASE_DIR/scripts/skill-manifest.sh" ]; then
    "$RELEASE_DIR/scripts/skill-manifest.sh" "$RELAY_HOME" "$RELAY_HOME/SKILLS.md" >/dev/null 2>&1 || \
        cp "$RELEASE_DIR/templates/SKILLS.md" "$RELAY_HOME/SKILLS.md"
else
    cp "$RELEASE_DIR/templates/SKILLS.md" "$RELAY_HOME/SKILLS.md"
fi

success "Identity files written"

# ── Initialise data files ─────────────────────────────────────────────
[ -f "$RELAY_HOME/data/sessions.json" ] || echo '{}' > "$RELAY_HOME/data/sessions.json"

TODAY=$(date +%Y-%m-%d)
[ -f "$RELAY_HOME/data/memory/$TODAY.md" ] || \
    printf "# %s\n\n" "$TODAY" > "$RELAY_HOME/data/memory/$TODAY.md"

# ── Maintenance scripts ───────────────────────────────────────────────
mkdir -p "$RELAY_HOME/scripts/maintenance"
if ls "$RELEASE_DIR"/scripts/maintenance/*.sh >/dev/null 2>&1; then
    cp "$RELEASE_DIR"/scripts/maintenance/*.sh "$RELAY_HOME/scripts/maintenance/"
    chmod +x "$RELAY_HOME"/scripts/maintenance/*.sh
fi

# ── macOS LaunchAgent ─────────────────────────────────────────────────
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo ""
    echo -n "  Install as macOS LaunchAgent (auto-start at login)? [y/N]: "
    read -r LAUNCHAGENT
    if [[ "$LAUNCHAGENT" == "y" || "$LAUNCHAGENT" == "Y" ]]; then
        PLIST_LABEL="com.$(whoami).relay-$RELAY_NAME"
        PLIST_DST="$HOME/Library/LaunchAgents/$PLIST_LABEL.plist"
        PLIST_TEMPLATE="$RELEASE_DIR/templates/config/relay.plist.template"

        if [ -f "$PLIST_TEMPLATE" ]; then
            mkdir -p "$HOME/Library/LaunchAgents"
            sed \
                -e "s|__RELAY_HOME__|$RELAY_HOME|g" \
                -e "s|__PLIST_LABEL__|$PLIST_LABEL|g" \
                -e "s|__PATH__|/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin:$HOME/.local/bin|g" \
                "$PLIST_TEMPLATE" > "$PLIST_DST"

            launchctl load "$PLIST_DST" 2>/dev/null && \
                success "LaunchAgent installed ($PLIST_LABEL)" || \
                warn "LaunchAgent install failed — run: launchctl load $PLIST_DST"
        else
            warn "relay.plist.template not found — skipping LaunchAgent"
        fi
    fi
fi

# ── Register in multi-agent registry ─────────────────────────────────
REGISTRY="$HOME/.relay"
touch "$REGISTRY"
if grep -q "^${RELAY_NAME}=" "$REGISTRY" 2>/dev/null; then
    grep -v "^${RELAY_NAME}=" "$REGISTRY" > "$REGISTRY.tmp" && mv "$REGISTRY.tmp" "$REGISTRY"
fi
echo "${RELAY_NAME}=${RELAY_HOME}" >> "$REGISTRY"
success "Registered $RELAY_NAME in ~/.relay"

# ── Record installed version ──────────────────────────────────────────
git -C "$RELEASE_DIR" rev-parse --short=8 HEAD 2>/dev/null \
    > "$RELAY_HOME/config/.version" || echo "unknown" > "$RELAY_HOME/config/.version"

# ── Done ──────────────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}${BOLD}  $AGENT_NAME installed at $RELAY_HOME${NC}"
echo ""
echo "  Next steps:"
echo ""
echo "  1. Install your LLM CLI (example for Claude):"
echo "       npm install -g @anthropic-ai/claude-code"
echo "       claude                    # authenticate"
echo ""
echo "  2. Personalize your agent:"
echo "       $RELAY_HOME/IDENTITY.md   # personality"
echo "       $RELAY_HOME/USER.md       # your context"
echo "       $RELAY_HOME/PRIORITIES.md # current focus"
echo ""
echo "  3. Start the daemon:"
echo "       $RELAY_HOME/bin/relay start -f     # foreground (see logs)"
echo "       $RELAY_HOME/bin/relay start        # background"
echo "       $RELAY_HOME/bin/relay log          # tail the log"
echo ""
if [[ "$WITH_MEMORY" == "0" ]]; then
    echo "  Optional: add memory/FAISS system later:"
    echo "       ./install.sh --name $RELAY_NAME --home $RELAY_HOME --with-memory"
    echo ""
fi
echo "  Registered agents:"
while IFS='=' read -r n h; do
    [[ "$n" =~ ^[[:space:]]*# ]] && continue
    [[ -z "$n" ]] && continue
    echo "    $n → $h"
done < "$REGISTRY"
echo ""


