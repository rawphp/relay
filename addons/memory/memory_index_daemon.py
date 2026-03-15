#!/usr/bin/env python3
"""
Memory Index Daemon — keeps FAISS search index fresh.

Monitors memory files for changes and rebuilds the FAISS index
when new or modified files are detected. Runs alongside relay.

Check interval: 300s (5 min) — index rebuilds are heavier than
lightweight daemon checks, so we don't poll as aggressively.

NOTE: The actual index build (which loads torch/numpy/faiss) runs in a
subprocess so those heavy libraries are freed from memory after each
rebuild, keeping this daemon at ~15MB instead of ~1.3GB.
"""

import json
import os
import sys
import time
import signal
import subprocess
from pathlib import Path
from datetime import datetime

# Resolve RELAY_HOME — no heavy lib imports at module level
RELAY_HOME = Path(os.environ.get("RELAY_HOME", os.path.expanduser("~/relay")))

# Paths (replicated from build_index.py to avoid importing it)
MEMORY_DIR = RELAY_HOME / "data" / "memory"
INDEX_DIR = MEMORY_DIR / ".index"
TIMESTAMP_PATH = INDEX_DIR / "last_build.timestamp"

CHECK_INTERVAL = 300  # Check every 5 minutes
RUNNING = True


def log(msg):
    """Timestamped log line."""
    print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] {msg}", flush=True)


def signal_handler(signum, frame):
    """Handle shutdown gracefully."""
    global RUNNING
    log(f"Received signal {signum}, shutting down...")
    RUNNING = False


def needs_rebuild(timestamp_path: Path, memory_dir: Path, relay_home: Path) -> bool:
    """
    Check if any memory file is newer than the last index build.

    Uses only stdlib — no torch/numpy/faiss imported here.

    Args:
        timestamp_path: Path to the last_build.timestamp file
        memory_dir: Directory containing daily memory .md files
        relay_home: relay root (also checked for MEMORY.md)
    """
    if not timestamp_path.exists():
        return True

    try:
        last_build = float(timestamp_path.read_text().strip())
    except (ValueError, IOError):
        return True

    # Collect memory files (mirrors build_index.get_memory_files logic)
    memory_files = sorted(memory_dir.glob("*.md")) if memory_dir.exists() else []
    main_memory = relay_home / "MEMORY.md"
    if main_memory.exists():
        memory_files.append(main_memory)

    if not memory_files:
        return False

    return any(f.stat().st_mtime > last_build for f in memory_files)


def get_last_build_ts(timestamp_path: Path) -> float:
    """
    Return the Unix timestamp of the last index build, or 0.0 if unavailable.

    Args:
        timestamp_path: Path to the last_build.timestamp file
    """
    try:
        return float(timestamp_path.read_text().strip())
    except (ValueError, IOError, OSError):
        return 0.0


def get_changed_files(
    memory_files: list,
    last_build_ts: float,
    file_state: dict,
) -> list:
    """
    Return list of (path, action, bytes_added) for files modified since
    last_build_ts.

    Args:
        memory_files: list of Path objects to check
        last_build_ts: Unix timestamp of last index build
        file_state: dict mapping str(path) -> (mtime, size) from previous cycle

    Returns:
        List of (Path, action_str, bytes_added_int) tuples.
        action is "created" if path was not in file_state, else "modified".
        bytes_added is max(0, current_size - previous_size) for modified files,
        or current_size for new files.
    """
    changed = []
    for f in memory_files:
        try:
            stat = f.stat()
        except OSError:
            continue
        if stat.st_mtime > last_build_ts:
            key = str(f)
            prev = file_state.get(key)
            if prev is None:
                action = "created"
                bytes_added = stat.st_size
            else:
                action = "modified"
                bytes_added = max(0, stat.st_size - prev[1])
            changed.append((f, action, bytes_added))
    return changed


def append_audit_log(audit_path: Path, entries: list) -> None:
    """
    Append memory change events to the audit log (JSONL format).

    Each entry is written as a JSON object on its own line:
        {"ts": <unix_float>, "file": <basename>, "action": <str>, "bytes_added": <int>}

    Args:
        audit_path: Path to data/memory-audit.jsonl
        entries: list of (path, action, bytes_added) tuples
    """
    if not entries:
        return
    audit_path.parent.mkdir(parents=True, exist_ok=True)
    ts = time.time()
    with open(audit_path, "a") as fh:
        for path, action, bytes_added in entries:
            record = {
                "ts": ts,
                "file": Path(path).name,
                "action": action,
                "bytes_added": bytes_added,
            }
            fh.write(json.dumps(record) + "\n")


def run_build(relay_home: Path) -> bool:
    """
    Spawn build_index.py as a subprocess so torch/numpy/faiss load and
    free in an isolated process — daemon stays at ~15MB.

    Returns True on success, False on failure or timeout.
    """
    # Prefer shared venv (multi-agent), fall back to legacy local venv
    shared_python = Path.home() / ".relay-shared" / "venv" / "bin" / "python3"
    local_python = relay_home / "lib" / "memory" / "venv" / "bin" / "python3"
    venv_python = shared_python if shared_python.exists() else local_python
    build_script = relay_home / "lib" / "memory" / "build_index.py"

    try:
        result = subprocess.run(
            [str(venv_python), str(build_script), "--force"],
            capture_output=True,
            text=True,
            timeout=300,
            env={**os.environ, "RELAY_HOME": str(relay_home)},
        )
        if result.returncode == 0:
            return True
        log(f"Build subprocess failed (exit {result.returncode}): {result.stderr[-500:]}")
        return False
    except subprocess.TimeoutExpired:
        log("ERROR: build_index.py timed out after 300s")
        return False
    except Exception as e:
        log(f"ERROR launching build subprocess: {e}")
        return False


def main():
    """Main daemon loop."""
    global RUNNING

    signal.signal(signal.SIGTERM, signal_handler)
    signal.signal(signal.SIGINT, signal_handler)

    log("Memory Index Daemon starting...")
    log(f"RELAY_HOME: {RELAY_HOME}")
    log(f"Check interval: {CHECK_INTERVAL}s")

    AUDIT_PATH = RELAY_HOME / "data" / "memory-audit.jsonl"

    # file_state: maps str(path) -> (mtime, size) — updated after each rebuild
    file_state: dict = {}

    # Initial build if no index exists
    if not TIMESTAMP_PATH.exists():
        log("No index found, building initial index...")
        if run_build(RELAY_HOME):
            log("Initial index built")
        else:
            log("ERROR: Initial index build failed")

    while RUNNING:
        try:
            if needs_rebuild(TIMESTAMP_PATH, MEMORY_DIR, RELAY_HOME):
                # Collect changed files before rebuild (so we capture pre-build state)
                memory_files = sorted(MEMORY_DIR.glob("*.md")) if MEMORY_DIR.exists() else []
                main_memory = RELAY_HOME / "MEMORY.md"
                if main_memory.exists():
                    memory_files.append(main_memory)
                last_build_ts = get_last_build_ts(TIMESTAMP_PATH)
                changed = get_changed_files(memory_files, last_build_ts, file_state)

                log("Memory files changed, rebuilding index...")
                if run_build(RELAY_HOME):
                    log("Index rebuilt successfully")
                    # Update file_state and write audit entries
                    if changed:
                        append_audit_log(AUDIT_PATH, changed)
                        log(f"Audit: {len(changed)} file(s) logged to {AUDIT_PATH.name}")
                    for f in memory_files:
                        try:
                            st = f.stat()
                            file_state[str(f)] = (st.st_mtime, st.st_size)
                        except OSError:
                            pass
                else:
                    log("Index rebuild failed — will retry next cycle")
            else:
                log("Index is fresh, no rebuild needed")
        except Exception as e:
            log(f"ERROR: {e}")

        # Sleep in 1-second ticks (allows clean shutdown)
        for _ in range(CHECK_INTERVAL):
            if not RUNNING:
                break
            time.sleep(1)

    log("Memory Index Daemon stopped")


if __name__ == "__main__":
    main()
