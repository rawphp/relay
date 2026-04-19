# Architecture

## The problem

I wanted one agent that could drive Claude Code, OpenAI Codex, and Gemini CLI from the same conversation. It had to hold persistent state across sessions and daemon restarts, and be controllable from my phone while I was away from the keyboard. None of the existing options did all four.

Claude Code itself runs in a terminal; closing the tab loses the session, and mobile access is via a web client that doesn't drive my local filesystem. Agent frameworks (AutoGen, LangChain, CrewAI) target in-process orchestration. They don't front an interactive CLI from a messenger. Telegram-to-ChatGPT bridges stream chat but can't execute against a workspace or switch tools mid-conversation.

relay is the glue that was missing: a daemon that owns the lifetime, owns the session store, and treats Claude Code, Codex, and Gemini as pluggable subprocesses behind one chat interface.

## Why C

C is an unfashionable choice for an LLM tool in 2026. The reasons I picked it anyway:

1. **Process lifetime.** relay runs for weeks at a time. No GC pauses during a 40-minute Claude Code run, no interpreter memory creep on a box that's also my dev machine. The 263 KB binary stays resident for the life of the login session.
2. **Subprocess and IPC control.** The core job is spawning CLIs, plumbing stdin/stdout, and routing Unix-socket messages between peers. Those are syscalls. A thin layer over `fork`/`exec`/`poll`/`AF_UNIX` is less code than wrapping them in a runtime.
3. **One static binary, anywhere.** `./install.sh` on a fresh Mac or Linux box. No runtime install, no version pinning, no Python 3.11-vs-3.12 surprises.

The cost: slower iteration than Python, harder onboarding for contributors, no ergonomic JSON handling (cJSON works but is chatty). I accepted that because the surface is small (43 source files, ~12.5k LOC) and the hot paths (polling, socket I/O, session state) are what C is good at.

## Architecture

relay is a single-threaded event loop. One process per agent. Multiple agents can run side-by-side on the same host, each with its own home directory. Configuration lives in `relay.conf`, session state in `data/sessions.json`, peer advertisements in `~/.relay.d/`, and offline messages queue in `~/.relay.d/inbox/{agent}/pending.jsonl` (the dead drop).

```
    Telegram Bot API (long-poll, up to 30s)
              │
              ▼
    ┌──────────────────────┐       ┌──────────────────────┐
    │   event_loop.c       │◄─────►│   agent_bus.c        │
    │  (single thread)     │       │   AF_UNIX socket     │
    └──────────┬───────────┘       └──────────┬───────────┘
               │                              │
               ▼                              ▼
    ┌──────────────────────┐       ┌──────────────────────┐
    │  workspace_resolver  │       │  peer_registry       │
    │  session manager     │       │  ~/.relay.d/*.json   │
    └──────────┬───────────┘       └──────────────────────┘
               │
               ▼
    ┌──────────────────────┐
    │  llm_provider.c      │
    │  ├─ claude.c         │   spawn CLI subprocess
    │  ├─ openai_codex.c   │   inject identity + memory
    │  └─ gemini.c         │   capture session_id
    └──────────────────────┘
```

**One request end-to-end.** You type "what's the status of the build?" into Telegram from your phone:

1. `telegram.c` returns the message from a long-poll cycle.
2. `event_loop.c` checks auth (your user ID is in the `relay.conf` config file) and asks `workspace_resolver.c` for the active workspace (say `/space relay` from an earlier message, persisted in `sessions.json`).
3. `session.c` looks up the session key `claude:{chat_id}:relay`. If it exists, the stored Claude session ID is passed to `claude --resume`.
4. `llm_prompt.c` prepends `SOUL.md`, `IDENTITY.md`, `USER.md`, `PRIORITIES.md`, and curated `MEMORY.md` to the user's text.
5. `claude.c` forks, execs the `claude` binary in the workspace directory, streams JSONL stdout. The new session ID is captured and written back to `sessions.json`.
6. The reply goes out via `telegram_send_message()`. The exchange is appended to `data/transcripts/{chat}.jsonl`.

Every one of those steps is behind a DI struct (`relay_http_t`, `relay_fs_t`, `relay_proc_t`, `relay_clock_t`). The test suite in `relay-daemon/tests/mocks.h` swaps them for in-memory implementations. No module calls `open()` or `fork()` directly. That rule is how 573 tests stay hermetic without a docker-compose.

The provider call itself is deliberately boring: provider choice lives in `relay.conf`, failures surface through return codes, and the new session ID rides out on the response struct so the caller decides whether to persist it.

```c
/* event_loop.c — dispatch one user turn to the configured provider */
llm_response_t resp;
memset(&resp, 0, sizeof(resp));

int rc = llm_provider_send_with_retry(
    loop->deps.llm, prompt, session_id, &resp);

if (rc != RELAY_OK || resp.is_error) {
    log_warn("llm failed rc=%d err=%s", rc, resp.error);
    return handle_llm_error(loop, chat_id, &resp);
}

/* Persist the returned session_id — only on success, only for this
 * chat+workspace key. Bus traffic takes a different path and never
 * writes here (see decision #2). */
if (resp.session_id[0])
    session_set(loop->deps.sessions, session_key, resp.session_id);
```

## Hard decisions

**1. Decentralised peer discovery over a broker.**
Each agent writes `~/.relay.d/{name}.json` on startup (`{pid, socket_path, started}`) and removes it on shutdown. Every 60 s, each event loop rescans the directory and prunes entries whose PID is dead (`kill(pid, 0) == ESRCH`).

- Alternative: a central broker process owning discovery and routing.
- Why: a broker becomes a single point of failure for a fleet of 3–5 agents on one box. Filesystem advertisements are authoritative (the owning process writes its own) and self-healing (dead PIDs clean themselves). Cost: ~60 s of peer-discovery latency, fine for async agent chat.

**2. Fresh LLM sessions for bus traffic, persistent sessions for humans.**
Human↔agent conversations resume via `claude --resume <session_id>`. Agent↔agent messages spawn a new session each time and inline the sender context in the prompt.

- Alternative: one persistent session per peer pair.
- Why: agent-to-agent chat has unbounded depth potential. Shared sessions caused runaway context growth and session-ID drift when a peer had been offline. Fresh-per-exchange gives a circuit breaker for free (combined with a depth field capped at 3) and makes the bus stateless.

**3. LLMs as subprocesses, not HTTP clients.**
`llm_provider.c` spawns `claude`, `codex`, or `gemini` rather than calling the Anthropic, OpenAI, or Google APIs directly.

- Alternative: maintain an HTTP+SSE client per vendor.
- Why: the CLIs already handle auth, streaming, tool use, file edits, and model routing. Re-implementing that is a year of work I'd rather not spend. Cost: ~50 ms subprocess overhead per turn and a hard dependency on each CLI being installed. Worth it.

## Measurements

Measured on an M2 MacBook Air, release build, macOS 15:

- Source: 43 C files, ~12,500 LOC in `src/`, ~2,100 LOC in headers, ~13,000 LOC of tests (573 test cases).
- Binary: 263 KB, `-std=c11 -Wall -Wextra -Werror -pedantic`, dynamically linked against libcurl and bundled cJSON. One executable, no runtime install.
- Cold start: daemon up and polling Telegram in ~180 ms.
- Poll cycle: the event loop iterates once per Telegram long-poll (up to 30 s blocking) or immediately on inbound activity. Agent bus has ≤30 s latency for the same reason (acceptable for async agent chat).
- Memory: ~4 MB RSS idle, ~12 MB during an active Claude session (subprocess pipes + transcript buffers).
- Concurrent agents: tested with 4 on one host. Peer discovery is O(n) in file count; the bus accept path is rate-limited to 10 connections/sec.
- Session durability: conversations survive daemon restart, kernel panic, and `update.sh` redeploys. State lives in `data/sessions.json` and is only ever appended.

## What's missing

Honest list:

- Windows is unsupported. The Unix-socket agent bus and fork/exec assumptions don't port cleanly.
- Encryption at rest for session files and transcripts isn't implemented. Files live at mode 0600 but unencrypted.
- The memory sidecar is Python. It's optional, but the dependency undermines the single-binary story. Rewriting it in C (or as a sibling daemon) is the obvious next step.
- Coverage is not published. The 573-test suite passes on every build; I haven't wired gcov into CI and I don't want to claim a number I can't cite.
- Multi-agent scaling on a single host beyond ~6 agents hasn't been tested. Per-peer rescan is O(n) and the dead-drop inbox is unbounded in file count.

Next on the list: voice-in on Telegram (pipeline exists, not wired end-to-end), and replacing the Python memory sidecar.
