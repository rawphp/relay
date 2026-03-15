#!/bin/bash
# speak.sh — Convert text to speech via ElevenLabs API
# Usage: ./speak.sh "Text to speak" [output_file.mp3]
# Config: reads from relay.conf (elevenlabs_api_key, elevenlabs_voice_id, elevenlabs_model_id)

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

# --- Load config ---
API_KEY=$(read_config "elevenlabs_api_key")
VOICE_ID=$(read_config "elevenlabs_voice_id" "JBFqnCBsd6RMkjVDRZzb")
MODEL_ID=$(read_config "elevenlabs_model_id" "eleven_flash_v2_5")

if [ -z "$API_KEY" ]; then
    echo "ERROR: elevenlabs_api_key not set in $CONFIG_FILE" >&2
    echo "Add your API key: elevenlabs_api_key = your_key_here" >&2
    exit 1
fi

# --- Args ---
if [ $# -lt 1 ]; then
    echo "Usage: speak.sh \"Text to speak\" [output_file.mp3]" >&2
    exit 1
fi

TEXT="$1"
OUTPUT="${2:-/tmp/relay_speech_$(date +%s).mp3}"

# --- Call ElevenLabs ---
HTTP_CODE=$(curl -s -w "%{http_code}" -o "$OUTPUT" \
    -X POST "https://api.elevenlabs.io/v1/text-to-speech/${VOICE_ID}" \
    -H "xi-api-key: ${API_KEY}" \
    -H "Content-Type: application/json" \
    -d "$(cat <<EOF
{
    "text": $(printf '%s' "$TEXT" | python3 -c 'import sys,json; print(json.dumps(sys.stdin.read()))'),
    "model_id": "${MODEL_ID}",
    "voice_settings": {
        "stability": 0.5,
        "similarity_boost": 0.75,
        "speed": 1.0
    }
}
EOF
)")

if [ "$HTTP_CODE" -ne 200 ]; then
    echo "ERROR: ElevenLabs API returned HTTP $HTTP_CODE" >&2
    # Output file contains the error response
    cat "$OUTPUT" >&2
    rm -f "$OUTPUT"
    exit 1
fi

# --- Verify output ---
FILE_SIZE=$(wc -c < "$OUTPUT" | tr -d ' ')
if [ "$FILE_SIZE" -lt 100 ]; then
    echo "ERROR: Output file suspiciously small (${FILE_SIZE} bytes)" >&2
    rm -f "$OUTPUT"
    exit 1
fi

echo "$OUTPUT"
