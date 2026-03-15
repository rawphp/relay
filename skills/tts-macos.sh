#!/bin/bash
# tts-macos.sh — Offline TTS via macOS say + ffmpeg + optional Telegram delivery
# Usage: ./tts-macos.sh "Text to speak" [--telegram] [output_file.ogg]
# Fallback for speak.sh when ElevenLabs is unavailable

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIG_FILE="${RELAY_CONFIG:-$SCRIPT_DIR/../config/relay.conf}"

# --- Config reader ---
read_config() {
    local key="$1"
    local default="${2:-}"
    local value
    value=$(grep -E "^${key}\s*=" "$CONFIG_FILE" 2>/dev/null | head -1 | cut -d'=' -f2- | xargs)
    if [ -n "$value" ]; then
        echo "$value"
    else
        echo "$default"
    fi
}

# --- Args ---
if [ $# -lt 1 ]; then
    echo "Usage: tts-macos.sh \"Text to speak\" [--telegram] [output_file.ogg]" >&2
    exit 1
fi

TEXT="$1"
shift

SEND_TELEGRAM=0
OUTPUT=""

for arg in "$@"; do
    case "$arg" in
        --telegram) SEND_TELEGRAM=1 ;;
        *) OUTPUT="$arg" ;;
    esac
done

[ -z "$OUTPUT" ] && OUTPUT="/tmp/relay_tts_$(date +%s).ogg"

# --- Check dependencies ---
if ! command -v say &>/dev/null; then
    echo "ERROR: say command not found (macOS only)" >&2
    exit 1
fi

if ! command -v ffmpeg &>/dev/null; then
    echo "ERROR: ffmpeg not found — install with: brew install ffmpeg" >&2
    exit 1
fi

# --- Generate speech ---
AIFF_FILE="/tmp/relay_tts_$(date +%s).aiff"
say -o "$AIFF_FILE" "$TEXT"

# --- Convert AIFF to OGG/Opus ---
ffmpeg -y -i "$AIFF_FILE" -c:a libopus -b:a 64k "$OUTPUT" 2>/dev/null
rm -f "$AIFF_FILE"

# --- Verify output ---
FILE_SIZE=$(wc -c < "$OUTPUT" | tr -d ' ')
if [ "$FILE_SIZE" -lt 100 ]; then
    echo "ERROR: Output file suspiciously small (${FILE_SIZE} bytes)" >&2
    rm -f "$OUTPUT"
    exit 1
fi

# --- Optional Telegram delivery ---
if [ "$SEND_TELEGRAM" -eq 1 ]; then
    BOT_TOKEN=$(read_config "telegram_bot_token")
    CHAT_ID=$(read_config "telegram_user_id")

    if [ -z "$BOT_TOKEN" ] || [ -z "$CHAT_ID" ]; then
        echo "ERROR: telegram_bot_token or telegram_user_id not set in config" >&2
        echo "$OUTPUT"
        exit 1
    fi

    HTTP_CODE=$(curl -s -w "%{http_code}" -o /dev/null \
        -X POST "https://api.telegram.org/bot${BOT_TOKEN}/sendVoice" \
        -F "chat_id=${CHAT_ID}" \
        -F "voice=@${OUTPUT}")

    if [ "$HTTP_CODE" -ne 200 ]; then
        echo "ERROR: Telegram API returned HTTP $HTTP_CODE" >&2
        echo "$OUTPUT"
        exit 1
    fi

    rm -f "$OUTPUT"
    echo "OK"
else
    echo "$OUTPUT"
fi
