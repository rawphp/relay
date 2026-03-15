# Development Guide

## Build Commands

All commands run from the `relay-daemon/` directory:

```bash
cd relay-daemon

make          # build the relay binary
make test     # build and run all tests
make clean    # remove build/ and relay binary
```

The production binary is output to `relay-daemon/relay`.

## Build System

The Makefile uses a wildcard rule — **all `.c` files in `src/` are automatically compiled**. Adding a new source file does not require any Makefile changes:

```makefile
SRC = $(wildcard src/*.c)   # automatically finds all src/*.c
```

Compiler flags: `-Wall -Wextra -Werror -pedantic -std=c11`

::: warning Warnings are errors
All warnings are treated as errors (`-Werror`). Fix every warning before committing.
:::

## TDD Workflow

All new code must be test-driven. Write the failing test first, then implement.

### 1. Write the failing test

Create or edit `relay-daemon/tests/test_<module>.c`:

```c
#include "Unity/unity.h"
#include "mocks.h"        /* always include */
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

### 2. Register the suite

If it's a **new module**, declare and call the suite in `tests/test_runner.c`:

```c
// Declaration:
void test_mymodule_suite(void);

// In main():
test_mymodule_suite();
```

For an **existing module**, just add `RUN_TEST(...)` calls inside the existing suite function.

### 3. Run — confirm red

```bash
make test    # must fail before you write any implementation
```

### 4. Implement, then confirm green

```bash
make test    # all tests must pass (zero failures)
```

**Definition of done:** `make test` passes with zero failures. Every new public function has a happy-path test + at least one error/edge-case test.

## Dependency Injection Pattern

All external dependencies — HTTP, filesystem, process spawning, clock — are injected via structs defined in `relay.h`. **No module calls system functions directly.**

```c
/* Wrong — not testable */
void my_func(const char *path) {
    FILE *f = fopen(path, "r");  // direct syscall — cannot mock
}

/* Right — testable */
void my_func(relay_fs_t *fs, const char *path) {
    if (!fs->file_exists(path)) return;
    char *content = fs->read_file(path);
}
```

When a function can't be tested without mocks, it must accept its dependencies via the DI structs.

## Available Mocks

All mocks are in `tests/mocks.h`.

### `g_mock_clock` — time control

```c
g_mock_time = 1708070400;  // set the current unix timestamp
// g_mock_clock.now() and g_mock_clock.localtime_r() use this value
```

### `g_mock_fs` — in-memory filesystem

```c
mock_fs_reset();                          // clear all files
mock_fs_set("/path/to/file", "content");  // create/overwrite a file
// .read_file, .write_file, .file_exists, .append_file, .delete_file
// all operate on the in-memory store — no disk I/O
// .list_dir(dir, suffix, names, max) — returns matching filenames (used by session discovery)
```

### `g_mock_http` — HTTP responses

```c
mock_http_set_response("{\"ok\":true}", 0);  // 0 = success, non-zero = error
// g_mock_http_last_url     — last URL that was called
// g_mock_http_last_body    — last request body sent
```

### `g_mock_proc` — process spawning

```c
mock_proc_set_output("LLM response text", 0);  // set subprocess "output"
// g_mock_proc.spawn_streaming() returns this output
```

## Key C Gotchas

### Variables must be declared before `goto` labels

`-Werror` turns this into a build failure:

```c
/* WRONG */
int my_func(void)
{
    if (bad) goto cleanup;
    int x = 5;      // declared AFTER a goto — compiler error
    cleanup:
    return -1;
}

/* RIGHT — declare all variables at the top of the function */
int my_func(void)
{
    int x;
    if (bad) goto cleanup;
    x = 5;
    cleanup:
    return -1;
}
```

### `main.c` and `event_loop.c` are excluded from test builds

These are excluded from `TESTABLE_SRC` in the Makefile. Code that needs to be unit-tested must live in separate modules.

### `RELAY_ERR_NOTFOUND` vs `RELAY_ERR`

`RELAY_ERR_NOTFOUND (-2)` means "nothing here right now" — not a fatal error. The agent bus uses this to signal EAGAIN. Check for it explicitly:

```c
int rc = agent_bus_accept_message(...);
if (rc == RELAY_ERR_NOTFOUND) continue;   /* no message waiting — normal */
if (rc != RELAY_OK) { /* actual error */ }
```

## Adding a New Module

1. Create `relay-daemon/src/mymodule.c` and `relay-daemon/src/mymodule.h`
2. Create `relay-daemon/tests/test_mymodule.c` with `test_mymodule_suite()`
3. Declare and call `test_mymodule_suite()` in `tests/test_runner.c`
4. Run `make test` — confirm red (no implementation yet)
5. Implement until `make test` passes green

No Makefile changes needed — `src/mymodule.c` is auto-discovered.

**For Telegram command handlers:** Follow the same pattern but also register the command in the `commands[]` array in `telegram.c` and add dispatch logic in `event_loop.c`. See `cmd_workspace.c` and `cmd_sessions.c` for examples.

## Adding a New LLM Provider

1. Create `relay-daemon/src/newprovider.c` and `relay-daemon/src/newprovider.h`
2. Add the backend enum and routing in `llm_provider.c`
3. Add config keys (e.g. `newprovider_binary`) to `templates/config/relay.conf.template`
4. Write tests in `tests/test_newprovider.c`

## Documentation Deployment

The docs site at `rawphp.github.io/relay/` is built with [VitePress](https://vitepress.dev/) and deployed automatically via GitHub Actions.

**How it works:** On every push to `main` that changes files in `docs/`, the `.github/workflows/deploy-docs.yml` workflow builds the VitePress site and deploys it to GitHub Pages.

**One-time setup (required):** In the GitHub repo settings, go to **Settings > Pages** and set the **Source** to **GitHub Actions** (not "Deploy from a branch"). Without this, the workflow will run but the site won't be published.

**Local preview:**

```bash
npm run docs:dev      # dev server at localhost:5173
npm run docs:build    # production build to docs/.vitepress/dist/
npm run docs:preview  # preview the production build
```

## Adding a New Message Platform

1. Create `relay-daemon/src/newplatform.c`
2. Add `newplatform_get_updates()` and `newplatform_send_message()` calls in `event_loop.c`
3. Add any new config keys to `relay.conf.template`
