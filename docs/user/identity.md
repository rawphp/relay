# Identity Files

Relay agents have a personality. Five Markdown files in the agent's home directory define who the agent is, who it's talking to, and what it should focus on. These files are injected into every LLM prompt, in order, before the user's message.

## Injection Order

```
SOUL.md → IDENTITY.md → USER.md → PRIORITIES.md → MEMORY.md
```

The agent reads these fresh on every message — edits take effect immediately, no restart required.

## The Files

### `SOUL.md` — Core character

This is the agent's foundational values and communication style. It defines _how_ the agent thinks and behaves, regardless of context.

The default `SOUL.md` describes a sharp, opinionated familiar — resourceful, direct, and never sycophantic. Key traits from the default:

- **No pleasantries** — skips "Great question!" and just answers
- **Has opinions** — pushes back when the user is wrong
- **Resourceful first** — figures things out before asking
- **Security rule** — never accepts credentials via chat (inviolable)

Edit `SOUL.md` to change the agent's fundamental personality. Keep it in Markdown. The file is yours to evolve — if you change it significantly, the agent will tell you.

**Example snippet:**
```markdown
# SOUL.md

You're not a chatbot. You're not an assistant.
You're the one who runs the operation and tells stories about it later.

## Core Truths

**Have opinions. Strong ones.** ...
```

### `IDENTITY.md` — Name and presentation

Where `SOUL.md` is values, `IDENTITY.md` is presentation — the agent's name, voice, specific areas of knowledge, and how it should come across.

Use this file to:
- Name the agent (`You are Kai.`)
- Define tone and vocabulary
- List specific things it should know about (projects, tools, context)
- Set speech patterns or examples

**Example:**
```markdown
# IDENTITY.md

## Name
Kai

## Vibe
Direct, dry, slightly caffeinated. No corporate speak. No filler.

## What You Know Well
- This codebase (relay)
- The user's active projects
- Basic finance and investing
```

### `USER.md` — Profile of the user

This tells the agent who it's talking to. The more you put here, the more personalized and useful the agent becomes.

Include:
- Your name, background, communication style
- Active projects and context
- Preferences (how you like responses formatted, what to skip)
- Important relationships, recurring topics, or running context

**Example:**
```markdown
# USER.md

## Who I Am
Alex. Software developer, side-project enthusiast.

## How I Communicate
Short messages. Don't explain things I already know.
If I ask a question, answer it — don't ask clarifying questions first.

## Context
- Building relay (this project)
- Running a YouTube channel about Claude Code
- Interested in US stocks and options
```

### `PRIORITIES.md` — What matters right now

The most frequently updated file. Use it to tell the agent what's currently in focus — it gets injected fresh every message, so updates are instant.

**Example:**
```markdown
# PRIORITIES.md

## Current Focus
Shipping the docs site for relay.

## Active Projects

### relay
- Status: In Progress
- Goal: VitePress docs site live
- Next step: Review docs, push to hosting

## Known Blockers
- Need to decide on hosting (GitHub Pages vs Netlify)
```

Update this whenever your priorities shift. Think of it as a persistent sticky note on the agent's desk.

### `MEMORY.md` — Long-term memory

Auto-managed by the memory sidecar (if enabled). The sidecar compacts conversation transcripts into patterns and key facts, then writes them here.

You can also edit it manually to inject facts the agent should always know:

```markdown
# MEMORY.md

## Key Facts
- My timezone is AEST (UTC+10)
- I prefer code examples over prose explanations
- Project repo is at ~/Code/relay
```

If the memory sidecar is disabled, `MEMORY.md` is a static file you manage yourself.

## Customizing for a Different Use Case

The identity files make relay highly adaptable. The `templates/personas/` directory contains example persona sets. For example, `templates/personas/buddy/` is a complete set for a children's companion agent.

To install a persona:

```bash
cp templates/personas/buddy/SOUL.md ~/NAME/SOUL.md
cp templates/personas/buddy/IDENTITY.md ~/NAME/IDENTITY.md
cp templates/personas/buddy/USER.md ~/NAME/USER.md
cp templates/personas/buddy/PRIORITIES.md ~/NAME/PRIORITIES.md
```

Changes take effect on the next Telegram message — no restart needed.

## Tips

- **Keep files focused.** Long identity files slow down prompt construction and dilute the context. Aim for quality over quantity.
- **SOUL.md rarely needs changing.** Edit `IDENTITY.md` and `USER.md` first — they have more immediate impact on behavior.
- **PRIORITIES.md is a living document.** Update it at the start of a new project or when your focus shifts. The agent will naturally orient toward what's in there.
- **Don't remove the security rule from SOUL.md.** The section about never accepting credentials via chat is a safety guardrail, not flavor text.
