#!/bin/bash
# Wrapper: run memory_http.py with the shared relay venv
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENV="$HOME/.relay-shared/venv"
exec "$VENV/bin/python" "$SCRIPT_DIR/memory_http.py" "$@"
