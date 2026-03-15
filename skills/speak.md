---
name: speak
description: Text-to-speech via ElevenLabs — generate MP3 or send Telegram voice notes
entry_point: ~/relay/skills/speak.sh
requires:
  - config:elevenlabs_api_key
  - bin:curl
  - bin:ffmpeg
triggers:
  - "send audio"
  - "voice note"
  - "speak"
  - "text to speech"
  - "TTS"
fallback: tts-macos
---

# Speak Skill — ElevenLabs Text-to-Speech

**Purpose:** Convert text responses to audio using ElevenLabs TTS API. Enables voice output via Telegram voice notes or local audio files.

---

## Scripts

| Script | Purpose |
|--------|---------|
| `speak.sh` | Core TTS — text in, MP3 file out |
| `speak-telegram.sh` | TTS + send as Telegram voice note |

---

## Configuration

Add to `~/relay/config/relay.conf`:

```ini
# ── ElevenLabs TTS ─────────────────────────────────────────────────── #
elevenlabs_api_key = YOUR_API_KEY_HERE
elevenlabs_voice_id = JBFqnCBsd6RMkjVDRZzb
elevenlabs_model_id = eleven_flash_v2_5
```

### Config Values

| Key | Default | Description |
|-----|---------|-------------|
| `elevenlabs_api_key` | *(required)* | Your ElevenLabs API key |
| `elevenlabs_voice_id` | `JBFqnCBsd6RMkjVDRZzb` | Voice to use (default: George — warm, narration) |
| `elevenlabs_model_id` | `eleven_flash_v2_5` | TTS model (flash = fast + cheap) |

### Finding Voice IDs

List your available voices:
```bash
curl -s "https://api.elevenlabs.io/v1/voices" \
  -H "xi-api-key: YOUR_KEY" | jq '.voices[] | {name, voice_id}'
```

### Popular Male Voices

| Name | Style | Voice ID |
|------|-------|----------|
| George | Warm, narration | `JBFqnCBsd6RMkjVDRZzb` |
| Charlie | Conversational, relaxed | *(query API)* |
| Brian | Deep, serious | *(query API)* |
| Chris | Casual, easy going | *(query API)* |
| Daniel | Authoritative, commanding | *(query API)* |

---

## Usage

### Generate audio file
```bash
~/relay/skills/speak.sh "Here's your schedule for tomorrow."
# Output: /tmp/relay_speech_1740000000.mp3

~/relay/skills/speak.sh "Custom text" ~/Desktop/output.mp3
# Output: ~/Desktop/output.mp3
```

### Send as Telegram voice note
```bash
~/relay/skills/speak-telegram.sh "You've got 3 jobs tomorrow, first one's at 8am in Sydney."
# Sends voice note to the user's Telegram
```

### From a Claude Code session (agent using it)
```bash
# Generate and send voice response
~/relay/skills/speak-telegram.sh "Morning, Alex. Schedule looks clean — 4 jobs, no conflicts. First one's at 9. Want the details?"
```

---

## Dependencies

- `curl` — API calls
- `python3` — JSON escaping (for text with special characters)
- `ffmpeg` *(optional)* — converts MP3 to OGG/Opus for proper Telegram voice notes. Falls back to sending as audio document without it.

### Install ffmpeg (if needed)
```bash
brew install ffmpeg
```

---

## How It Works

1. `speak.sh` reads ElevenLabs config from `relay.conf`
2. Sends text to ElevenLabs TTS API
3. Saves MP3 audio to output path
4. `speak-telegram.sh` wraps this:
   - Generates MP3 via `speak.sh`
   - Converts to OGG/Opus via ffmpeg (if available)
   - Sends as voice note via Telegram Bot API

---

## Notes

- **Cost**: ElevenLabs charges per character. `eleven_flash_v2_5` is the cheapest model.
- **Latency**: Flash model generates ~1-2 seconds for short text. Longer text scales linearly.
- **Voice settings**: Stability 0.5, similarity 0.75, speed 1.0. Tweak in `speak.sh` if needed.
- **Credential security**: API key lives ONLY in `relay.conf` (chmod 600). Never pass through chat.
