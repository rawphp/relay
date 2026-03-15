# Security Policy

## Supported Versions

| Version | Supported |
|---------|-----------|
| latest  | Yes       |

We support the latest release on the `main` branch. Older versions do not receive security patches.

## Reporting a Vulnerability

If you discover a security vulnerability, please report it responsibly:

1. **Do not** open a public issue
2. Email **security@originalsolutions.com.au** or use GitHub's [private vulnerability reporting](https://docs.github.com/en/code-security/security-advisories/guidance-on-reporting-and-writing-information-about-vulnerabilities/privately-reporting-a-security-vulnerability)
3. Include steps to reproduce, impact assessment, and any suggested fixes

We aim to acknowledge reports within 48 hours and provide a fix timeline within 7 days.

## Security Design

relay handles sensitive credentials and runs as a persistent daemon. Here's how the project approaches security:

### Secrets Management

- **No hardcoded secrets** — all tokens and API keys are read from `relay.conf` at runtime
- **Config file permissions** — `relay.conf` is created with `chmod 600` (owner-only read/write)
- **Template placeholders** — install scripts use `{{PLACEHOLDER}}` substitution, never ship real values

### Daemon Security

- **Restrictive umask** — the daemon sets `umask(0077)` at startup so all created files are owner-only
- **PID file isolation** — PID file is stored in `$RELAY_HOME/data/`, not world-writable `/tmp`
- **Connection rate limiting** — the agent bus enforces connection rate limits
- **NULL guards** — all `getenv()` calls are guarded against NULL returns

### Build Security

- **Strict compiler flags** — `-Wall -Wextra -Werror -pedantic` catches potential issues at compile time
- **C11 standard** — no reliance on undefined behavior or compiler-specific extensions

## Dependency Policy

relay has minimal external dependencies:

- **C standard library** — no third-party C libraries beyond the Unity test framework (dev only)
- **curl** — used for HTTP requests to Telegram and LLM APIs (system-installed)
- **LLM CLIs** — Claude Code, Codex, or Gemini CLI (user-installed, not bundled)

We do not bundle or vendor runtime dependencies, reducing supply chain risk.
