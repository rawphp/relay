# Skills

Skills are reusable capabilities you can install into your agent's workspace. They extend what your agent can do without modifying the core daemon.

## Structure

```
skills/
└── my-skill/
    ├── install.sh     ← Copies files into ~/relay/
    ├── uninstall.sh   ← Removes them
    └── ...            ← Skill files (scripts, prompts, configs)
```

## Installing a Skill

```bash
skills/my-skill/install.sh
```

## Creating a Skill

A skill can be anything: a prompt template, a shell script, a Claude Code custom command, or a configuration snippet. The install script copies it into the right place in `~/relay/`.

Example `install.sh`:
```bash
#!/bin/bash
SKILL_DIR="$(cd "$(dirname "$0")" && pwd)"
RELAY_HOME="${RELAY_HOME:-$HOME/relay}"

cp "$SKILL_DIR/my-prompt.md" "$RELAY_HOME/skills/my-skill/"
echo "✓ my-skill installed"
```
