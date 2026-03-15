# Contributing to relay

Thanks for your interest in contributing to relay! This guide covers everything you need to get started.

## Prerequisites

- macOS or Linux
- Xcode Command Line Tools (macOS) or gcc (Linux)
- `curl`
- `make`

## Building

```bash
cd relay-daemon
make
```

The build uses `-Wall -Wextra -Werror -pedantic -std=c11` — all warnings are errors. If it doesn't compile cleanly, it doesn't ship.

## Testing

```bash
cd relay-daemon
make test
```

Tests use the [Unity](https://github.com/ThrowTheSwitch/Unity) framework. All tests must pass before any commit.

## Development Workflow

1. **Fork** and clone the repository
2. **Create a branch** for your change: `git checkout -b my-feature`
3. **Write tests first** — we follow TDD strictly:
   - Add tests to `relay-daemon/tests/test_<module>.c`
   - Run `make test` — confirm they fail (red)
   - Write the minimum implementation to pass (green)
4. **Verify** the full test suite passes: `cd relay-daemon && make test`
5. **Commit** with a clear message (see below)
6. **Open a pull request** against `main`

## Test Structure

Tests live in `relay-daemon/tests/`. Follow the existing pattern:

```c
#include "Unity/unity.h"
#include "mocks.h"
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

void test_mymodule_suite(void)
{
    RUN_TEST(test_my_feature_happy_path);
}
```

Dependencies are injected via structs (`relay_fs_t`, `relay_clock_t`, etc.) — if a function can't be tested with mocks, refactor it to accept its dependencies.

## Code Style

- C11 standard
- 4-space indentation
- Declare variables before the first `goto` in a function (avoids `-Werror` failures)
- New `.c` files in `src/` are auto-discovered by the Makefile — no Makefile changes needed

## Commit Messages

Use this format:

```
type(scope): short description

Longer explanation if needed.
```

Types: `feat`, `fix`, `refactor`, `test`, `docs`, `chore`

## Pull Request Checklist

- [ ] All tests pass (`cd relay-daemon && make test`)
- [ ] No compiler warnings (the build enforces this)
- [ ] New public functions have tests (happy path + error case)
- [ ] Commit messages follow the convention above

## Reporting Bugs

Open an issue with:
- What you expected to happen
- What actually happened
- Steps to reproduce
- Your OS and compiler version

## Questions?

Open a discussion or issue — we're happy to help.
