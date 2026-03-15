# Architecture

Relay is a single-threaded C daemon that polls Telegram for messages, routes them to an LLM (Claude Code by default), and sends responses back — persistently, in the background.

## Data Flow

```
Telegram API
    │
    │  long-poll (up to 30s)
    ▼
Event Loop  ──────────────────────────────────────────────►  Agent Bus
    │                                                        (Unix socket)
    │  lookup/create session key
    ▼
Session Manager
    │
    │  inject identity files + memory
    ▼
LLM Provider
    │  spawn subprocess (Claude Code / Gemini / OpenAI)
    ▼
LLM Response
    │
    │  send reply
    ▼
Telegram API
```

## Top-Level Components

### Event Loop (`event_loop.c`)

The heart of the daemon. A single-threaded loop that:

1. **Polls Telegram** for new messages (blocks up to 30 seconds per poll)
2. **Routes each message** — checks auth, resolves workspace, picks handler
3. **Calls the LLM provider** and waits for a response
4. **Sends the reply** back to Telegram
5. **Checks the agent bus** for inter-agent messages each iteration

The loop also handles: config reload on `SIGHUP`, spinner animations while Claude is thinking, reaction/interruption events, and memory auto-flush on idle.

### Telegram Poller (`telegram.c`)

Wraps the Telegram Bot API (long polling via HTTP). Key operations:
- `telegram_get_updates()` — fetch new messages, photos, reactions
- `telegram_send_message()` — send text replies
- `telegram_send_chat_action()` — "typing…" indicator

### LLM Provider (`llm_provider.c`)

Abstraction layer over multiple backends:

| Backend | File | When used |
|---------|------|-----------|
| Claude Code | `claude.c` | Default — spawns `claude` CLI subprocess |
| Gemini | `gemini.c` | Configured via `relay.conf` |
| OpenAI Codex | `openai_codex.c` | Configured via `relay.conf` |

Before sending a prompt, the provider **injects identity files** from the agent's workspace directory in this order: `SOUL.md` → `IDENTITY.md` → `USER.md` → `PRIORITIES.md` → `MEMORY.md`. This gives the LLM its character and long-term memory context before the user's message.

**Invocation pattern:**
```c
llm_response_t resp;
memset(&resp, 0, sizeof(resp));
int rc = llm_provider_send_with_retry(loop->deps.llm, prompt, session_id, &resp);
if (rc != RELAY_OK || resp.is_error) { /* handle error */ }
// resp.result = LLM text, resp.session_id = updated session ID
```

### Session Manager (`session.c`)

Tracks active LLM sessions per Telegram chat. Sessions are persisted to `~/relay/data/sessions.json` so conversations survive daemon restarts. A session key is derived from the chat ID (and optionally the workspace), enabling multi-workspace agents with separate conversation contexts.

### Agent Bus

Inter-agent communication via Unix domain sockets. Multiple relay agents can exchange messages autonomously — the daemon handles everything without human intervention.

#### Core Protocol (`agent_bus.c`)

Each agent listens on a Unix domain socket (`AF_UNIX`, `SOCK_STREAM`). The socket is non-blocking — `agent_bus_accept_message()` returns `RELAY_ERR_NOTFOUND` when no message is waiting (EAGAIN), so it never stalls the main loop. Checked once per event loop iteration.

Messages are JSON with these fields:

| Field | Type | Purpose |
|-------|------|---------|
| `from` | string | Sender agent name |
| `from_socket` | string | Sender's socket path (for replies) |
| `text` | string | Message content (max 4096 chars) |
| `ts` | number | Unix timestamp |
| `msg_id` | string | Unique message ID |
| `depth` | int | Conversation depth (circuit breaker) |
| `is_autonomous` | int | 1 = AI-initiated, 0 = user-initiated |
| `addressed_to` | string | Target agent name ("all"/"team" = broadcast) |

Rate limiting (`agent_bus_rate.c`) enforces max connections per second (default: 10).

#### Peer Discovery (`peer_registry.c`, `agent_advertise.c`)

Agents discover each other through self-advertisement:

1. **On startup**, each agent writes `~/.relay.d/{name}.json`:
   ```json
   {"name": "kai", "pid": 12345, "socket": "/Users/tom/kai/data/relay.sock", "started": 1710500000}
   ```
2. **On shutdown**, the file is removed.
3. **Every 60 seconds**, the event loop re-scans `~/.relay.d/` for new/removed advertisements.
4. **Stale entries** (dead PID via `kill(pid, 0)`) are automatically cleaned up during scanning.

This model means agents never read each other's configs — each agent is authoritative about its own socket path.

#### Message Flow

When an agent bus message arrives:

```
Inbound message (from peer socket)
    │
    ├── Log to agent-bus.jsonl
    ├── Check depth < max_depth (circuit breaker, default: 3)
    ├── Check addressed_to (selective response)
    │
    ├── Resolve workspace (same as Telegram messages)
    ├── Call LLM (fresh session, no --resume)
    │
    ├── Send reply back to sender's socket
    ├── Notify human via Telegram: "[Bus] Ash → Kai: <preview>"
    └── Append to today's memory file
```

Key design decisions:
- **Fresh sessions** — bus messages don't use `--resume`. Each exchange is self-contained with context in the prompt. This avoids stale session errors.
- **Separate session tracks** — human conversations use `claude:{chat_id}`, bus uses `agent-bus:{peer}`. No session merging to avoid interjection.
- **Human notification** — after every successful bus exchange, the authorized Telegram user gets a `[Bus]` notification so they can observe without being a relay.

#### Bus Directives (`bus_directive.c`)

The LLM can initiate agent-to-agent communication by emitting directives in its response:

```
[AGENT_BUS_SEND to=ash] Hey Ash, Tom wants to know about your status.
```

The daemon (`process_bus_directives()` in `event_loop.c`):
1. Strips the directive from the Telegram-bound response
2. Looks up the target in the peer registry
3. Sends via `agent_bus_send()` if online, or saves to dead drop if offline
4. Appends a status note: "(sent message to ash)" or "(agent offline — message saved)"

Integrated into all response paths: streaming final-send, dispatch_llm_response (documents/photos), and reaction handler. NOT applied to streaming paragraph flushes or bus-to-bus replies.

The bus prompt explicitly tells the LLM "Reply directly. Do NOT use [AGENT_BUS_SEND]" to prevent replies from containing redundant directives.

#### Dead Drop (`bus_dead_drop.c`)

When a target agent's daemon is down, messages are persisted instead of dropped:

- **Save**: `~/.relay.d/inbox/{target}/pending.jsonl` — JSONL with from, text, ts, msg_id
- **Load on startup**: daemon checks its inbox, processes pending messages through the LLM, sends replies, notifies the human
- **Clear**: inbox file deleted after processing

This ensures agents never miss messages across daemon restarts.

#### Key Files

| File | Purpose |
|------|---------|
| `agent_bus.c` | Socket protocol: init, accept, send, log, destroy |
| `agent_bus_rate.c` | Connection rate limiter |
| `agent_advertise.c` | Write/remove `~/.relay.d/{name}.json` |
| `peer_registry.c` | Scan ads, PID liveness, build LLM context block |
| `bus_directive.c` | Parse `[AGENT_BUS_SEND]` from LLM output |
| `bus_dead_drop.c` | Offline message persistence |
| `pending_bus_messages.c` | Per-workspace retry queue for LLM failures |

### Command Handlers (`cmd_workspace.c`, `cmd_sessions.c`)

Telegram commands are dispatched in the event loop to dedicated handler modules:

- **`cmd_workspace.c`** — handles `/space`, `/spaces`, `/workspace`, `/close`, `/clear`. Manages workspace switching, listing, and session clearing.
- **`cmd_sessions.c`** — handles `/sessions` and `/session <N>`. Discovers resumable Claude Code sessions and allows the user to select one by number.

Commands return early from the event loop, preventing the message from being forwarded to the LLM.

### Workspace Resolver (`workspace_resolver.c`)

Resolves the active workspace for a given chat using a fallback chain:

1. **Active workspace** — the user's last `/space` selection (persisted per chat ID in `sessions.json`)
2. **First `[workspace]` block** — auto-selected if no explicit selection exists
3. **Global `workspace_path` key** — legacy fallback for configs without `[workspace]` blocks
4. **Install directory** — derived from the config file path (e.g. `~/relay/config/relay.conf` → `~/relay`)

### Session Discovery (`session_discovery.c`)

Discovers resumable Claude Code sessions from the current workspace's `.claude/projects/` directory:

1. **Path encoding** (`path_util.c`): workspace path is encoded to Claude's naming scheme (`/Users/tom/project` → `-Users-tom-project`)
2. **File scanning**: lists `.jsonl` files in `~/.claude/projects/<encoded-path>/`
3. **Summary extraction**: reads the first non-system user message from each transcript (max 80 chars)
4. **Caching**: summaries are cached in `~/.relay-session-cache.json` (version 3 format, invalidated on extraction logic changes)

Provider-gated — only Claude Code supports session browsing. Other providers return a "not supported" message.

### Memory System (`memory_sidecar.c`, `memory_search.c`, `memory_curator.c`)

An optional Python sidecar process (`lib/memory/`) that:
- **Indexes** conversation transcripts
- **Curates** a compressed `MEMORY.md` injected into every LLM prompt
- **Searches** past conversations when relevant context is needed

The C daemon spawns and manages the sidecar lifecycle. If the sidecar is absent, relay continues without memory — it's not a hard dependency.

### Dependency Injection (`relay.h`)

All external dependencies — HTTP, filesystem, process spawning, clock — are abstracted behind structs (`relay_http_t`, `relay_fs_t`, `relay_proc_t`, `relay_clock_t`). The real implementations live in `main.c`; the test suite in `tests/mocks.h` provides mock implementations. **No module calls system functions directly** — they accept their dependencies via these structs.

The filesystem struct (`relay_fs_t`) includes a `list_dir()` function for listing files by suffix, used by session discovery to scan for `.jsonl` session files.

## Runtime Layout

```
~/relay/
├── bin/relay          ← daemon binary
├── config/relay.conf  ← secrets (chmod 600)
├── data/
│   ├── sessions.json  ← active Claude Code sessions
│   └── transcripts/   ← full message history (.jsonl)
├── SOUL.md            ← agent character
├── IDENTITY.md        ← agent identity
├── USER.md            ← user profile for this agent
├── PRIORITIES.md      ← agent priorities
└── MEMORY.md          ← curated long-term memory
```

Multiple agents can run independently — each has its own home directory.

**Shared directories** (across all agents):
```
~/.relay              ← agent registry (name=home_path, written by install.sh)
~/.relay.d/           ← peer discovery advertisements
├── kai.json          ← Kai's advertisement (PID, socket path)
├── ash.json          ← Ash's advertisement
└── inbox/            ← dead drop for offline messages
    └── kai/
        └── pending.jsonl
```

## Error Codes

Defined in `relay.h`:

| Code | Value | Meaning |
|------|-------|---------|
| `RELAY_OK` | 0 | Success |
| `RELAY_ERR` | -1 | Generic error |
| `RELAY_ERR_NOTFOUND` | -2 | Not found (e.g. no bus message waiting) |
| `RELAY_ERR_PARSE` | -3 | Parse failure |
| `RELAY_ERR_TIMEOUT` | -4 | Operation timed out |
| `RELAY_ERR_AUTH` | -5 | Unauthorized |
| `RELAY_ERR_IO` | -6 | I/O error |
| `RELAY_ERR_NOMEM` | -7 | Out of memory |
| `RELAY_ERR_INVALID` | -8 | Invalid argument |
| `RELAY_ERR_NETWORK` | -9 | Network error |
| `RELAY_ERR_FULL` | -10 | Buffer/queue full |
