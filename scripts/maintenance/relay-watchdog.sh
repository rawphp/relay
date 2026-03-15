#!/bin/bash
# relay-watchdog.sh — launchd watchdog for relay daemon
#
# This script stays alive as long as relay is running.
# launchd manages THIS script. This script manages relay.
#
# Flow:
#   1. Start relay if not running
#   2. Monitor relay in a loop (check every 10s)
#   3. If relay dies, restart it
#   4. If THIS script is killed, relay keeps running (it's a daemon)

# Auto-detect relay home from this script's location (scripts/maintenance/ -> root)
RELAY_HOME="${RELAY_HOME:-$(cd "$(dirname "$0")/../.." && pwd)}"
# Read pid_file from agent's relay.conf — supports multi-agent installs where
# each agent has its own pid path (e.g. /tmp/relay-henry.pid).
# Falls back to /tmp/relay.pid for legacy single-agent installs.
PIDFILE=$(grep "^pid_file" "$RELAY_HOME/config/relay.conf" 2>/dev/null | cut -d= -f2 | tr -d ' ')
PIDFILE="${PIDFILE:-/tmp/relay.pid}"
RELAY_BIN="$RELAY_HOME/bin/relay"
# Fallback to build directory if bin/relay doesn't exist yet
[ -f "$RELAY_BIN" ] || RELAY_BIN="$RELAY_HOME/relay-daemon/build/relay"
CHECK_INTERVAL=10

is_relay_running() {
    if [ -f "$PIDFILE" ]; then
        PID=$(cat "$PIDFILE" 2>/dev/null)
        if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
            return 0
        fi
    fi
    return 1
}

start_relay() {
    "$RELAY_BIN" start
    sleep 2  # give it time to daemonize and write PID file
}

# Start relay if not already running
if ! is_relay_running; then
    start_relay
fi

# Monitor loop — stay alive, check relay every 10s
while true; do
    sleep "$CHECK_INTERVAL"

    if ! is_relay_running; then
        start_relay
    fi
done
