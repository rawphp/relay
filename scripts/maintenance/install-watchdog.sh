#!/bin/bash
# Install relay as a macOS LaunchAgent (auto-start + auto-restart)

set -e

# Auto-detect relay home from this script's location (scripts/install/ -> root)
RELAY_HOME="$(cd "$(dirname "$0")/../.." && pwd)"
PLIST_LABEL="com.$(whoami).relay"
PLIST_TEMPLATE="$RELAY_HOME/config/relay.plist.template"
PLIST_GENERATED="/tmp/relay-generated.plist"
PLIST_DST="$HOME/Library/LaunchAgents/$PLIST_LABEL.plist"

echo "==> Installing relay watchdog from $RELAY_HOME..."

# Generate plist from template
PATH_VALUE="/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin:$HOME/.local/bin"
sed \
    -e "s|__RELAY_HOME__|$RELAY_HOME|g" \
    -e "s|__PLIST_LABEL__|$PLIST_LABEL|g" \
    -e "s|__PATH__|$PATH_VALUE|g" \
    "$PLIST_TEMPLATE" > "$PLIST_GENERATED"

# Stop existing instance if running
if [ -f /tmp/relay.pid ]; then
    PID=$(cat /tmp/relay.pid 2>/dev/null)
    if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
        echo "==> Stopping current relay instance..."
        kill "$PID" || true
        sleep 1
    fi
fi

# Unload existing LaunchAgent if installed
if launchctl list | grep -q "$PLIST_LABEL"; then
    echo "==> Unloading existing LaunchAgent..."
    launchctl unload "$PLIST_DST" 2>/dev/null || true
fi

# Install generated plist
echo "==> Installing LaunchAgent as $PLIST_LABEL..."
mkdir -p "$HOME/Library/LaunchAgents"
cp "$PLIST_GENERATED" "$PLIST_DST"

# Load LaunchAgent
echo "==> Loading LaunchAgent..."
launchctl load "$PLIST_DST"

# Wait for startup
sleep 2

# Verify it's running
if launchctl list | grep -q "$PLIST_LABEL"; then
    echo "✅ relay watchdog installed successfully!"
    echo ""
    echo "relay will now:"
    echo "  - Start automatically at boot"
    echo "  - Restart automatically if it crashes"
    echo "  - Throttle restarts (10s minimum between restarts)"
    echo ""
    echo "Useful commands:"
    echo "  launchctl list | grep relay              # Check status"
    echo "  launchctl unload $PLIST_DST    # Disable watchdog"
    echo "  launchctl load $PLIST_DST      # Enable watchdog"
    echo "  tail -f $RELAY_HOME/logs/launchd-stdout.log  # View logs"
else
    echo "❌ relay failed to start. Check logs:"
    echo "  tail $RELAY_HOME/logs/launchd-stderr.log"
    exit 1
fi
