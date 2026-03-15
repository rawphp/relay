---
name: speak-telegram
description: Text-to-speech via ElevenLabs, delivered as a Telegram voice note
entry_point: ~/relay/skills/speak-telegram.sh
requires:
  - config:elevenlabs_api_key
  - config:telegram_bot_token
  - config:telegram_user_id
  - bin:curl
  - bin:ffmpeg
triggers:
  - "voice note"
  - "send voice"
  - "speak telegram"
  - "voice message"
---

# Speak Telegram — Voice Note Delivery

**Purpose:** Generate speech from text using ElevenLabs TTS and deliver it as a Telegram voice note in one step.

Wraps `speak.sh` (core TTS) and adds OGG/Opus conversion + Telegram delivery.

## Usage

```bash
~/relay/skills/speak-telegram.sh "Good morning, Alex. You've got 3 jobs today."
# → Generates MP3 via ElevenLabs → converts to OGG → sends as Telegram voice note
```

## Dependencies

- `speak.sh` — core TTS engine (must be in same directory)
- `curl` — API calls to ElevenLabs and Telegram
- `ffmpeg` — converts MP3 to OGG/Opus for Telegram voice notes (falls back to audio document without it)
- `python3` — JSON escaping (inherited from speak.sh)

## Configuration

Uses the same ElevenLabs config as `speak.sh`, plus Telegram credentials from `relay.conf`:

```ini
elevenlabs_api_key = YOUR_KEY
telegram_bot_token = YOUR_BOT_TOKEN
telegram_user_id = YOUR_CHAT_ID
```
