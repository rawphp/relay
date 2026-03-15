# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

relay is a persistent AI familiar daemon — a C-based daemon that polls Telegram for messages, routes them to Claude Code as a subprocess, and sends responses back.

## TDD Policy — Mandatory

**All new code must be test-driven.** Write the failing test first, then write the minimum code to make it pass.

### C (Unity framework)

1. **Write the test first** in `relay-daemon/tests/test_<module>.c`
2. **Register the test function** inside the module's `*_suite()` function in that file
3. **If it's a new module**, create `tests/test_newmodule.c`, declare `void test_newmodule_suite(void);` in `test_runner.c`, and call it from `main()`
4. **Run** `cd relay-daemon && make test` — the build must fail first (red), then pass after implementation (green)
5. **100% coverage** of new public functions is required

**Test structure** (follow existing pattern exactly):
```c
#include "Unity/unity.h"
#include "mocks.h"        /* always include — provides mock clock, fs, http, proc */
#include "mymodule.h"

static void test_my_feature_happy_path(void)
{
    /* arrange */
    mock_fs_reset();
    mock_fs_set("/some/path", "content");

    /* act */
    int rc = my_function(&g_mock_fs, "/some/path");

    /* assert */
    TEST_ASSERT_EQUAL_INT(RELAY_OK, rc);
}

static void test_my_feature_missing_file(void)
{
    mock_fs_reset();  /* no files registered = file_exists returns 0 */
    int rc = my_function(&g_mock_fs, "/missing");
    TEST_ASSERT_EQUAL_INT(RELAY_ERR_NOTFOUND, rc);
}

void test_mymodule_suite(void)
{
    RUN_TEST(test_my_feature_happy_path);
    RUN_TEST(test_my_feature_missing_file);
}
```

**Available mocks** (from `tests/mocks.h`):
- `g_mock_clock` / `g_mock_time` — control time
- `g_mock_fs` / `mock_fs_set(path, content)` / `mock_fs_reset()` — in-memory filesystem
- HTTP and process-spawn mocks — see `mocks.h` for full API

**Design rule**: If a function can't be tested without mocks, it must accept its dependencies via the `relay_*_t` DI structs, not call system functions directly.

### What "done" means

- `cd relay-daemon && make test` passes with zero failures
- Every new public function has at least: a happy-path test, an error/edge-case test

---

## Build Commands

```bash
# Build the C daemon
cd relay-daemon && make

# Run C unit tests
cd relay-daemon && make test

# Clean build artifacts
cd relay-daemon && make clean

# Install (first-time setup)
./install.sh

# Deploy updates to running agents
./update.sh
```

## Running the Daemon

```bash
~/relay/bin/relay start -f    # foreground (logs to stdout)
~/relay/bin/relay start       # background daemon
~/relay/bin/relay stop        # stop daemon
~/relay/bin/relay restart     # restart daemon
~/relay/bin/relay refresh     # reload config without restart
~/relay/bin/relay status      # show if running + PID
~/relay/bin/relay log         # tail the log
~/relay/bin/relay config      # validate config file
```

## Architecture

### C Daemon (`relay-daemon/`)

**Build system**: `SRC = $(wildcard src/*.c)` — all `.c` files in `src/` are automatically compiled. New source files don't need Makefile changes.

**Compiler flags**: `-Wall -Wextra -Werror -pedantic -std=c11` — warnings are errors.

**Key C gotcha**: Variables declared after a `goto cleanup` label cause `-Werror` build failures. Always declare variables before the first `goto` in a function.

**Dependency injection**: All external dependencies (HTTP, process spawning, filesystem, clock) are injected via structs defined in `relay.h`. The test suite in `relay-daemon/tests/mocks.h` provides mock implementations. This is the primary testability pattern.

**Event loop** (`event_loop.c`): Single-threaded polling loop. `poll_telegram()` blocks up to 30 seconds — agent bus messages have up to 30s latency as a result.

**LLM invocation pattern**:
```c
llm_response_t resp;
memset(&resp, 0, sizeof(resp));
int rc = llm_provider_send_with_retry(loop->deps.llm, prompt, session_id, &resp);
if (rc != RELAY_OK || resp.is_error) { /* handle error */ }
if (resp.session_id[0]) session_set(loop->deps.sessions, session_key, resp.session_id);
// resp.result = LLM text
```

**Error codes** (from `relay.h`): `RELAY_OK=0`, `RELAY_ERR=-1`, `RELAY_ERR_NOTFOUND=-2`, `RELAY_ERR_PARSE=-3`, `RELAY_ERR_TIMEOUT=-4`, `RELAY_ERR_AUTH=-5`, `RELAY_ERR_IO=-6`, `RELAY_ERR_NOMEM=-7`, `RELAY_ERR_INVALID=-8`, `RELAY_ERR_NETWORK=-9`, `RELAY_ERR_FULL=-10`

**Agent bus** (`agent_bus.c`, `peer_registry.c`, `agent_advertise.c`, `bus_directive.c`, `bus_dead_drop.c`): Inter-agent messaging via Unix domain sockets. Agents advertise themselves to `~/.relay.d/{name}.json` on startup (PID, socket path). Peers scan this directory every 60s. `bus_directive.c` parses `[AGENT_BUS_SEND to=<name>]` directives from LLM output and routes messages. `bus_dead_drop.c` persists messages for offline agents in `~/.relay.d/inbox/`. `agent_bus_accept_message` returns `RELAY_ERR_NOTFOUND` (not `RELAY_ERR`) when no message is available (EAGAIN). Bus LLM calls use fresh sessions (no `--resume`) and the prompt says "Reply directly. Do NOT use AGENT_BUS_SEND" to prevent recursion.

**Command handlers**: Telegram commands are dispatched in `event_loop.c` to dedicated handlers:
- `cmd_workspace.c` — `/space`, `/spaces`, `/workspace`, `/close`, `/clear`
- `cmd_sessions.c` — `/sessions`, `/session <N>`

**Workspace resolver** (`workspace_resolver.c`): Resolves the active workspace with a fallback chain: active workspace → first `[workspace]` block → global `workspace_path` → install directory.

**Session discovery** (`session_discovery.c`): Scans `~/.claude/projects/` for `.jsonl` session files, extracts summaries (first user message), caches results in `~/.relay-session-cache.json`. Provider-gated — only Claude Code supports session browsing.

**Path encoding** (`path_util.c`): Converts filesystem paths to/from Claude's `.claude/projects/` directory naming scheme (e.g., `/Users/tom/project` → `-Users-tom-project`). Also resolves the relay install directory from config path.

**Workspace configuration** in `relay.conf` uses `[workspace]` blocks:
```ini
[workspace "myproject"]
path = ~/projects/myproject
provider = claude

[workspace "other"]
path = ~/projects/other
provider = openai_codex
```

### Runtime vs Source

- **Source repo**: `~/Code/relay-release/` (this repo)
- **Runtime home**: `~/relay/` (per-agent installation)
- **Agent registry**: `~/.relay` — maps `agent_name=~/agent/home`

`update.sh` rebuilds the daemon, stops running daemons, copies new binaries/scripts, and restarts — **preserving** all config, identity files, memories, and logs.

### Key Runtime Files

| File | Purpose |
|------|---------|
| `~/relay/config/relay.conf` | Main config — contains secrets (chmod 600) |
| `~/relay/data/sessions.json` | Session tracking + active workspace per chat |
| `~/relay/data/transcripts/*.jsonl` | Full message history |
| `~/relay/IDENTITY.md`, `USER.md`, `SOUL.md`, `PRIORITIES.md` | Agent identity files |

### Adding New Components

- **New Telegram command**: Add handler in `src/cmd_*.c`, dispatch in `event_loop.c`, register in `telegram.c` commands array
- **New message platform**: Add `src/newplatform.c`, integrate in `event_loop.c`
- **New LLM provider**: Add `src/newprovider.c`, register in `llm_provider.c`
- **New C source file**: Just create it in `src/` — Makefile auto-discovers it
