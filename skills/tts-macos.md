---
name: tts-macos
description: Offline text-to-speech via macOS say command — fallback when ElevenLabs is unavailable
entry_point: ~/relay/skills/tts-macos.sh
requires:
  - bin:say
  - bin:ffmpeg
triggers:
  - "speak offline"
  - "local TTS"
  - "say command"
  - "macOS speech"
---

# TTS macOS — Offline Text-to-Speech

**Purpose:** Generate speech using the macOS `say` command and optionally deliver as a Telegram voice note. Works completely offline — no API keys needed.

This is the fallback for the `speak` skill when ElevenLabs is unavailable.

## Usage

```bash
# Generate audio file only
~/relay/skills/tts-macos.sh "Good morning, Alex."
# → /tmp/relay_tts_TIMESTAMP.ogg

# Generate + send to Telegram
~/relay/skills/tts-macos.sh "Good morning, Alex." --telegram
# → Sends voice note to Telegram
```

## Dependencies

- `say` — macOS built-in TTS (not available on Linux)
- `ffmpeg` — converts AIFF output to OGG/Opus for Telegram voice notes
- `curl` — only needed for Telegram delivery

## Configuration

No API keys required for audio generation. For Telegram delivery, uses credentials from `relay.conf`:

```ini
telegram_bot_token = YOUR_BOT_TOKEN
telegram_user_id = YOUR_CHAT_ID
```
