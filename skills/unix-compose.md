---
name: unix-compose
description: Solve problems by composing small CLI tools with pipes instead of scripts
entry_point: ~/relay/skills/unix-compose.md
requires:
  - bin:jq
  - bin:curl
triggers:
  - "pipeline"
  - "compose commands"
  - "jq"
  - "unix pipe"
---

# Unix Composition Skill

**Purpose:** Solve problems by composing small CLI tools instead of writing monolithic scripts.

**Philosophy:** Each tool does ONE thing well. Combine them with pipes.

---

## Core Tools Available

```bash
jq          # JSON parsing/filtering/transformation
curl        # HTTP requests
grep/awk    # Text filtering/processing
sed         # Stream editing
sort/uniq   # Ordering/deduplication
xargs       # Argument builder
bc          # Calculator
date        # Time operations
```

---

## Pattern: Always Prefer Composition

### ❌ Bad: Monolithic Python
```python
# 237 lines to fetch, filter, transform, export
fetch_and_process_data.py
```

### ✅ Good: Composed Pipeline
```bash
curl -s api.com/data |           # Fetch
  jq '.items[]' |                 # Extract
  jq 'select(.status == "active")' |  # Filter
  jq -r '.name' |                 # Transform
  sort -u                         # Deduplicate
```

---

## When To Use This Skill

**Use composition when:**
- Fetching and filtering API data
- Processing JSON files
- Extracting specific fields from large datasets
- Transforming data formats
- Counting/aggregating values

**Don't use when:**
- Need complex stateful logic
- Require error recovery
- Building interactive tools
- Performance-critical (tight loops)

---

## Examples

### Extract plumber emails from SendGrid
```bash
curl -s "$SENDGRID_API/messages" \
  -H "Authorization: Bearer $KEY" | \
  jq '.messages[] | select(.subject | test("plumber"; "i"))' | \
  jq -r '.to_email' | \
  sort -u > plumbers.csv
```

### Check MyProject overdue invoices
```bash
MyProject invoices list | \
  jq '.[] | select(.status == "overdue")' | \
  jq -r '"\(.customer): $\(.amount)"' | \
  sort -t: -k2 -nr  # Sort by amount descending
```

### Find reminders due in next hour
```bash
NOW=$(date -u +%s)
HOUR_FROM_NOW=$(date -u -d '+1 hour' +%s)

cat reminders.json | \
  jq --argjson now "$NOW" \
     --argjson future "$HOUR_FROM_NOW" \
     '.reminders[] |
      select((.remind_at | fromdateiso8601) >= $now and
             (.remind_at | fromdateiso8601) <= $future)' | \
  jq -r '.task'
```

### Calculate total revenue from MyProject
```bash
MyProject invoices list | \
  jq '[.[].amount] | add'
```

---

## Token Optimization

**Always use `jq` to extract ONLY what you need:**

### ❌ Bad: Read entire 10MB file
```bash
cat huge_state.json  # Loads 10MB into context
```

### ✅ Good: Extract just what matters
```bash
cat huge_state.json | jq '.open_loops[0:5]'  # First 5 items only
cat huge_state.json | jq '.agents.fitness'    # Just fitness agent
cat huge_state.json | jq 'del(.git_activity)' # Remove noise
```

**Use `head`/`tail` to limit output:**
```bash
MyProject jobs list | jq -r '.[]' | head -20  # First 20 only
```

**Use `--compact-output` for smaller payloads:**
```bash
jq -c '.open_loops'  # One line instead of pretty-printed
```

---

## Building Block Pattern

Instead of one big script, build **building blocks**:

```bash
# relay-sessions - Output active sessions
#!/bin/bash
cat ~/relay/data/sessions.json

# relay-active - Extract active session IDs
#!/bin/bash
relay-sessions | jq -r '.[] | select(.active) | .id'

# Compose them
relay-active | xargs -I {} relay session show {}
```

---

## Common Patterns

### Pattern: Filter → Transform → Act
```bash
DATA_SOURCE | \
  jq 'select(CONDITION)' | \
  jq -r '.FIELD' | \
  while read item; do
    ACTION "$item"
  done
```

### Pattern: Aggregate
```bash
DATA_SOURCE | \
  jq -r '.FIELD' | \
  sort | uniq -c | sort -rn
```

### Pattern: Join data
```bash
# Get customer IDs from jobs
MyProject jobs list | jq -r '.[].customer_id' | \
  # Fetch customer details for each
  xargs -I {} MyProject customers get {} | \
  # Extract names
  jq -r '.name'
```

---

## Self-Improvement Exercise

Every time you write Python/script longer than 20 lines, ask:
1. Could this be a pipeline instead?
2. What's the smallest JSON I actually need?
3. Can I extract just the fields I care about?

**Goal:** Reduce context usage by 80% through surgical data extraction.

---

---

## Remember

**"Do one thing, do it well, compose with others."**

Every time you're about to write a loop in Python:
→ Can this be a pipeline instead?

Every time you load a JSON file:
→ Can I extract just the fields I need with `jq`?

Every time you format output:
→ Can `awk` or `column` do this?
