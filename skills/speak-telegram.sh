#!/bin/bash
# speak-telegram.sh — Generate speech and send as Telegram voice note
# Usage: ./speak-telegram.sh "Text to speak"
# Requires: speak.sh, telegram bot token + chat id from relay.conf

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

if [ $# -lt 1 ]; then
    echo "Usage: speak-telegram.sh \"Text to speak\"" >&2
    exit 1
fi

TEXT="$1"

# --- Generate audio ---
AUDIO_FILE=$("$SCRIPT_DIR/speak.sh" "$TEXT")

if [ $? -ne 0 ] || [ ! -f "$AUDIO_FILE" ]; then
    echo "ERROR: Failed to generate audio" >&2
    exit 1
fi

# --- Convert to OGG/Opus for Telegram voice notes ---
OGG_FILE="${AUDIO_FILE%.mp3}.ogg"
if command -v ffmpeg &>/dev/null; then
    ffmpeg -y -i "$AUDIO_FILE" -c:a libopus -b:a 64k "$OGG_FILE" 2>/dev/null
    rm -f "$AUDIO_FILE"
else
    # Fall back to sending as audio document if ffmpeg isn't available
    OGG_FILE="$AUDIO_FILE"
fi

# --- Send via Telegram ---
BOT_TOKEN=$(read_config "telegram_bot_token")
CHAT_ID=$(read_config "telegram_user_id")

if [ -z "$BOT_TOKEN" ] || [ -z "$CHAT_ID" ]; then
    echo "ERROR: telegram_bot_token or telegram_user_id not set in config" >&2
    rm -f "$OGG_FILE"
    exit 1
fi

# Use sendVoice for .ogg, sendAudio for .mp3
if [[ "$OGG_FILE" == *.ogg ]]; then
    ENDPOINT="sendVoice"
    FIELD="voice"
else
    ENDPOINT="sendAudio"
    FIELD="audio"
fi

HTTP_CODE=$(curl -s -w "%{http_code}" -o /dev/null \
    -X POST "https://api.telegram.org/bot${BOT_TOKEN}/${ENDPOINT}" \
    -F "chat_id=${CHAT_ID}" \
    -F "${FIELD}=@${OGG_FILE}")

rm -f "$OGG_FILE"

if [ "$HTTP_CODE" -ne 200 ]; then
    echo "ERROR: Telegram API returned HTTP $HTTP_CODE" >&2
    exit 1
fi

echo "OK"
