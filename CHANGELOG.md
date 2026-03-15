# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- MIT LICENSE file, CONTRIBUTING.md, CODE_OF_CONDUCT.md, SECURITY.md
- GitHub release preparation (issue templates, CI workflow)
- VitePress documentation site with GitHub Pages deployment
- Session discovery: `/sessions` command browses resumable Claude Code sessions from `.claude/projects/`
- Session selection: `/session <N>` command to resume a session by number from the listing
- Session summary extraction and caching (`~/.relay-session-cache.json`)
- Provider-aware session gating — only Claude Code supports session browsing
- Path encoding/decoding for `.claude/projects/` directory naming scheme
- Centralized install directory resolution with workspace fallback chain

### Changed

- Renamed workspace commands: `/session` → `/space`, `/sessions` → `/spaces`
- Updated Telegram command registration to reflect new command names (12 commands total)
- Workspace resolver now uses multi-level fallback: active workspace → first block → global path → install dir
- Sessions now track workspace context (name, path, provider) per chat

### Fixed

- Handle `/sessions` and `/spaces` gracefully when no workspace is configured
- GCC format-truncation and stringop-truncation compiler warnings suppressed
- Replaced `strncat` with `snprintf` offset pattern in workspace command handler
- Added `_DEFAULT_SOURCE` to CFLAGS for Linux/glibc portability
- Added missing `sys/time.h` include for `struct timeval`
- Reject identity injection blocks in session summaries

## [0.1.0] - 2026-03-14

Initial public release.

### Added

- **Core daemon**: Single-threaded C11 polling loop routing Telegram messages to LLM CLI subprocesses
- **Multi-workspace support**: Configure multiple project directories, switch between them from Telegram (`/space`, `/spaces`, `/workspace`)
- **Multi-provider support**: Per-workspace LLM backends — Claude Code, OpenAI Codex, Gemini CLI
- **Persistent sessions**: Conversations survive daemon restarts with session tracking
- **Identity system**: IDENTITY.md, USER.md, SOUL.md, PRIORITIES.md files for agent personality
- **Memory sidecar**: Optional semantic search over past conversations with automatic context injection
- **Agent bus**: Unix domain socket for inter-agent messaging with connection rate limiting
- **Skill system**: SKILLS.md manifest, skill capture command, YAML frontmatter metadata
- **Voice support**: TTS voice replies via macOS speech synthesis
- **Spinner animation**: Visible dot animation while waiting for LLM responses
- **Installer**: Interactive `install.sh` for first-time setup (name, Telegram creds, workspace config)
- **Update script**: `update.sh` rebuilds and redeploys to all registered agents
- **Telegram commands**: `/space`, `/spaces`, `/workspace`, `/sessions`, `/session`, `/clear`, `/close`, `/status`, `/reload`, `/restart`, `/help`, `/start`

### Security

- Restrictive `umask(0077)` at daemon startup
- Config file created with `chmod 600` (owner-only)
- PID file stored in `$RELAY_HOME/data/` instead of `/tmp`
- Connection rate limiting on agent bus
- NULL guards on all `getenv()` calls
- Bot token scrubbed from log output
- Auth token required for memory sidecar communication
- `--allowedTools` restriction on Claude invocations
- Safe `fork+exec` replacing `popen()` in voice handler
