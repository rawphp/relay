---
layout: home

hero:
  name: Relay
  text: Your persistent AI familiar
  tagline: A C daemon that bridges Telegram and Claude Code — always on, always listening.
  actions:
    - theme: brand
      text: Get Started
      link: /user/install
    - theme: alt
      text: Architecture
      link: /developer/architecture

features:
  - title: User Guide
    details: Install, configure, and operate the relay daemon. Covers setup, relay.conf, and all CLI commands.
    link: /user/install
  - title: Developer Guide
    details: Understand the architecture and extend the codebase. Covers the event loop, DI pattern, TDD workflow, and C gotchas.
    link: /developer/architecture
---

## Why VitePress?

This project's docs use [VitePress](https://vitepress.dev). It was chosen over Astro because:

- **Zero framework overhead** — pure Markdown + one config file
- **Purpose-built for developer tool docs** — not a general static site builder
- **Minimal setup** — no plugins, adapters, or framework concepts to learn
- **Astro** is a better fit for content-heavy sites and blogs; overkill for a C daemon's reference docs
