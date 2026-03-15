# Installation & Configuration

## Prerequisites

- macOS (tested) or Linux
- `make` and `cc` — install via Xcode Command Line Tools: `xcode-select --install`
- `curl` — usually pre-installed
- [Claude Code](https://claude.ai/code) CLI: `npm install -g @anthropic-ai/claude-code`
- A Telegram bot token — create one via [@BotFather](https://t.me/botfather)
- Your Telegram user ID — get it from [@userinfobot](https://t.me/userinfobot)

Optional (for the memory system):

- Python 3.9+ — `brew install python3`

## Install

Clone the repository and run the installer:

```bash
git clone git@github.com:rawphp/relay.git
cd relay
./install.sh
```

The installer will prompt for:
- **Agent name** — a slug like `kai` or `nova` (default: `kai`)
- **Install directory** — where the agent's runtime files live (default: `~/NAME`)
- Whether to include the memory/FAISS system (`--with-memory`)

Or pass arguments non-interactively:

```bash
./install.sh --name kai --home ~/kai
./install.sh --name kai --home ~/kai --with-memory
```

The installer:
1. Builds the C daemon (`relay-daemon/`)
2. Creates the runtime directory structure
3. Generates `relay.conf` from the template
4. Writes the `~/NAME/bin/relay` control script
5. Registers the agent in `~/.relay`

## Runtime Directory Layout

After installation:

```
~/NAME/
├── bin/relay          ← daemon control script
├── config/
│   └── relay.conf     ← main config (chmod 600 — contains secrets)
├── data/
│   ├── sessions.json  ← active LLM session IDs
│   ├── transcripts/   ← full message history (.jsonl per chat)
│   └── .telegram-photos/  ← downloaded photo cache
├── logs/
│   └── relay.log      ← daemon log
├── apps/
│   └── memory-py/     ← memory sidecar (if --with-memory was used)
├── SOUL.md            ← agent character (edit to customize)
├── IDENTITY.md        ← agent identity
├── USER.md            ← profile of the user talking to the agent
├── PRIORITIES.md      ← agent priorities
└── MEMORY.md          ← curated long-term memory (auto-managed)
```

The agent registry at `~/.relay` maps agent names to home directories:

```
kai=~/kai
nova=~/nova
```

## Configuring relay.conf

::: warning Security
`relay.conf` contains your Telegram bot token. Always keep it chmod 600:

```bash
chmod 600 ~/NAME/config/relay.conf
```
:::

### Required Keys

| Key | Description |
|-----|-------------|
| `telegram_bot_token` | Bot token from @BotFather |
| `telegram_user_id` | Your Telegram user ID (numeric) — only this user can send messages |
| `agent_name` | Agent slug, e.g. `kai` |
| `user_name` | Your name — injected into identity context |

### LLM Provider

| Key | Default | Description |
|-----|---------|-------------|
| `llm_provider` | `claude` | Backend: `claude`, `openai_codex`, or `gemini` |
| `claude_binary` | `~/.local/bin/claude` | Path to the `claude` CLI |
| `claude_timeout` | `900` | Seconds before Claude is killed |
| `openai_binary` | — | Path to the `codex` CLI (if using OpenAI) |
| `openai_model` | — | e.g. `gpt-4o` |
| `openai_sandbox` | `workspace-write` | `read-only`, `workspace-write`, `danger-full-access` |
| `gemini_binary` | — | Path to the `gemini` CLI |
| `gemini_model` | — | e.g. `gemini-2.5-flash` |

### Workspaces

Each workspace is a directory the agent can be sent into. Use `/session <name>` in Telegram to switch.

```ini
[workspace "myapp"]
path = ~/Code/myapp
provider = claude

[workspace "research"]
path = ~/Documents/research
provider = gemini
```

The `provider` key per workspace is optional — it overrides the top-level `llm_provider` for that workspace.

### Daemon Paths

| Key | Default | Description |
|-----|---------|-------------|
| `log_file` | `logs/relay.log` | Path to the log file |
| `pid_file` | `/tmp/relay-NAME.pid` | PID file location |
| `session_file` | `data/sessions.json` | Session persistence |
| `transcript_dir` | `data/transcripts` | Transcript storage |
| `photo_dir` | `data/.telegram-photos` | Downloaded photos |
| `session_expiry_hours` | `24` | Hours before an idle session expires |

### Memory System (optional)

| Key | Default | Description |
|-----|---------|-------------|
| `memory_search_enabled` | `0` | Enable semantic search over past conversations |
| `memory_service_url` | `http://localhost:8765` | Memory sidecar URL |
| `memory_service_autostart` | `0` | Auto-start the sidecar with the daemon |
| `memory_service_script` | `apps/memory-py/memory_http.py` | Path to sidecar script |
| `memory_search_top_k` | `3` | Number of memories to inject per message |
| `memory_search_threshold` | `0.5` | Minimum similarity score |

### Other Keys

| Key | Default | Description |
|-----|---------|-------------|
| `agent_bus_socket` | — | Unix socket path for inter-agent messaging |
| `telegram_group_chat_id` | — | Group chat ID for group mode |
| `parent_telegram_user_id` | — | Parent agent's user ID (for agent-to-agent alerts) |
| `vision_model` | `moondream` | Local vision model for photo analysis |
| `vision_ollama_url` | `http://localhost:11434` | Ollama server URL |

## Verify the Installation

```bash
~/NAME/bin/relay config    # validate relay.conf — shows parsed values
~/NAME/bin/relay status    # confirm daemon is stopped (not yet started)
```

If `relay config` shows your token and user ID correctly, you're ready. See [Operations](./operations) to start the daemon.
