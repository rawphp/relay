# Example Persona: Buddy

This is a **sample persona** demonstrating how relay's identity system works. It shows a child-friendly AI companion configuration — one of many possible use cases.

## What This Demonstrates

- **IDENTITY.md** — Name, personality, tone of voice, conversation style
- **USER.md** — Information about who the agent talks to
- **SOUL.md** — Core values, safety boundaries, parent alert system
- **PRIORITIES.md** — Current focus areas
- **CLAUDE.md** — Agent-level instructions for the LLM

## Creating Your Own Persona

1. Copy this directory: `cp -r templates/personas/buddy templates/personas/yourname`
2. Edit each file to match your use case
3. Point your workspace config at the new persona directory

Names, ages, interests, and details in these files are fictional examples. Replace them with your own.

See the [Identity Files documentation](https://rawphp.github.io/relay/user/identity) for the full guide.
