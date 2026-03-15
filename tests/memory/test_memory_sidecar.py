"""
Unit tests for apps/memory-py/memory_http.py

Tests use a _LinearIndex (numpy-only) and a fixed DummyEmbedder so no
sentence-transformers or faiss installation is required.
"""

import json
import math
import os
import sys
import tempfile
import time

import numpy as np
import pytest

# Make apps/memory-py importable from the repo root.
_REPO_ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
sys.path.insert(0, os.path.join(_REPO_ROOT, "apps", "memory-py"))

from memory_http import MemoryStore, _LinearIndex, _tokenize


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

DIM = 8  # small dimension for tests


def make_embed_fn(mapping: dict):
    """Return a deterministic embed_fn backed by a lookup table."""
    def embed(text: str) -> np.ndarray:
        if text in mapping:
            v = np.array(mapping[text], dtype="float32")
        else:
            # hash-based deterministic fallback
            rng = np.random.default_rng(abs(hash(text)) % (2**32))
            v = rng.random(DIM).astype("float32")
        norm = np.linalg.norm(v)
        return v / norm if norm > 0 else v
    return embed


# ---------------------------------------------------------------------------
# Test: /health
# ---------------------------------------------------------------------------

def test_health_empty_store():
    store = MemoryStore(data_dir=None,
                        embed_fn=make_embed_fn({}),
                        index=_LinearIndex(DIM))
    h = store.health()
    assert h["status"] == "ok"
    assert h["index_size"] == 0


def test_health_returns_ok_with_populated_index():
    """Deep health check succeeds when index has data and is queryable."""
    vec = [1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    embed_fn = make_embed_fn({"test probe": vec, "hello": vec})
    store = MemoryStore(data_dir=None, embed_fn=embed_fn,
                        index=_LinearIndex(DIM))
    store.upsert("probe-1", "hello", {"importance": 0.5})
    h = store.health()
    assert h["status"] == "ok"
    assert h["index_size"] == 1


def test_health_returns_error_when_index_broken():
    """Deep health check returns error status when index search raises."""
    class _BrokenIndex:
        def size(self):
            return 1
        def search(self, query, k):
            raise RuntimeError("corrupted index")
        def add(self, vec, fid):
            pass
        def remove(self, fid):
            pass

    embed_fn = make_embed_fn({})
    store = MemoryStore(data_dir=None, embed_fn=embed_fn,
                        index=_BrokenIndex())
    h = store.health()
    assert h["status"] == "error"
    assert "corrupted index" in h["detail"]


def test_health_returns_error_when_embed_fn_broken():
    """Deep health check returns error when embedding model fails."""
    vec = [1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    working_embed = make_embed_fn({"test": vec})

    store = MemoryStore(data_dir=None, embed_fn=working_embed,
                        index=_LinearIndex(DIM))
    store.upsert("x", "test", {"importance": 0.5})

    # Now break the embed_fn
    def broken_embed(text):
        raise RuntimeError("model not loaded")
    store._embed_fn = broken_embed

    h = store.health()
    assert h["status"] == "error"
    assert "model not loaded" in h["detail"]


# ---------------------------------------------------------------------------
# Test: upsert + search round-trip
# ---------------------------------------------------------------------------

def test_upsert_and_search_roundtrip():
    """Upsert a memory, then search for it by text — must be top result."""
    # Embed query and doc identically so cosine similarity = 1.0
    vec = [1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    embed_fn = make_embed_fn({
        "what is the project deadline": vec,
        "project deadline is Friday": vec,
    })
    store = MemoryStore(data_dir=None, embed_fn=embed_fn,
                        index=_LinearIndex(DIM))

    store.upsert("mem-001", "project deadline is Friday",
                 {"source": "daily_log", "importance": 0.8})

    results = store.search("what is the project deadline", top_k=5,
                           min_score=0.0)
    assert len(results) == 1
    assert results[0]["id"] == "mem-001"
    assert results[0]["text"] == "project deadline is Friday"
    assert results[0]["score"] > 0


def test_search_returns_ranked_results():
    """Best-matching doc should rank first."""
    exact_vec = [1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    weak_vec  = [0.1, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    embed_fn = make_embed_fn({
        "relay config": exact_vec,
        "relay config settings": exact_vec,
        "unrelated topic": weak_vec,
    })
    store = MemoryStore(data_dir=None, embed_fn=embed_fn,
                        index=_LinearIndex(DIM))

    store.upsert("strong", "relay config settings",
                 {"source": "daily_log", "importance": 0.9})
    store.upsert("weak",   "unrelated topic",
                 {"source": "daily_log", "importance": 0.1})

    results = store.search("relay config", top_k=2, min_score=0.0)
    assert results[0]["id"] == "strong"


def test_search_min_score_filters():
    """Results below min_score are excluded."""
    low_vec   = [0.01, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    query_vec = [1.0,  0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    embed_fn = make_embed_fn({
        "some query": query_vec,
        "distant memory": low_vec,
    })
    store = MemoryStore(data_dir=None, embed_fn=embed_fn,
                        index=_LinearIndex(DIM))

    store.upsert("far", "distant memory",
                 {"source": "daily_log", "importance": 0.5})

    # min_score=0.5 should exclude the low-similarity result
    results = store.search("some query", top_k=5, min_score=0.5)
    assert all(r["score"] >= 0.5 for r in results)


def test_search_strategy_field_hybrid():
    """strategy field is present and sensible when both vector and BM25 fire."""
    vec = [1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    embed_fn = make_embed_fn({
        "relay memory search": vec,
        "relay memory": vec,
    })
    store = MemoryStore(data_dir=None, embed_fn=embed_fn,
                        index=_LinearIndex(DIM))
    store.upsert("m1", "relay memory", {"importance": 0.5})
    results = store.search("relay memory search", top_k=5, min_score=0.0)
    assert len(results) > 0
    assert results[0]["strategy"] in ("hybrid", "bm25_only", "vector_only")


# ---------------------------------------------------------------------------
# Test: delete
# ---------------------------------------------------------------------------

def test_delete_removes_from_search():
    vec = [1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    embed_fn = make_embed_fn({
        "find me": vec,
        "hello world": vec,
    })
    store = MemoryStore(data_dir=None, embed_fn=embed_fn,
                        index=_LinearIndex(DIM))
    store.upsert("del-001", "hello world", {"importance": 0.5})

    assert store.delete("del-001") is True
    results = store.search("find me", top_k=5, min_score=0.0)
    assert all(r["id"] != "del-001" for r in results)


def test_delete_missing_returns_false():
    store = MemoryStore(data_dir=None, embed_fn=make_embed_fn({}),
                        index=_LinearIndex(DIM))
    assert store.delete("nonexistent-id") is False


# ---------------------------------------------------------------------------
# Test: health after upserts
# ---------------------------------------------------------------------------

def test_health_reflects_index_size():
    store = MemoryStore(data_dir=None, embed_fn=make_embed_fn({}),
                        index=_LinearIndex(DIM))
    store.upsert("a", "apple", {"importance": 0.5})
    store.upsert("b", "banana", {"importance": 0.5})
    h = store.health()
    assert h["index_size"] == 2


# ---------------------------------------------------------------------------
# Test: ingest_daily_logs
# ---------------------------------------------------------------------------

def test_ingest_daily_logs_basic(tmp_path):
    """ingest_daily_logs scans *.md files and creates memories."""
    # Write a daily log
    mem_dir = tmp_path / "data" / "memory"
    mem_dir.mkdir(parents=True)
    log_file = mem_dir / "2026-03-01.md"
    log_file.write_text("First paragraph has enough text to pass.\n\nSecond paragraph also has enough.\n")

    store = MemoryStore(data_dir=str(tmp_path),
                        embed_fn=make_embed_fn({}),
                        index=_LinearIndex(DIM))
    result = store.ingest_daily_logs(str(tmp_path))

    assert result["files_scanned"] == 1
    assert result["chunks_upserted"] >= 2  # two paragraphs
    assert result["chunks_skipped"] == 0


def test_ingest_daily_logs_skips_unchanged(tmp_path):
    """Re-ingesting the same file skips it (cursor prevents re-index)."""
    mem_dir = tmp_path / "data" / "memory"
    mem_dir.mkdir(parents=True)
    log_file = mem_dir / "2026-03-01.md"
    log_file.write_text("First paragraph has enough text to pass.\n\nSecond paragraph also has enough.\n")

    store = MemoryStore(data_dir=str(tmp_path),
                        embed_fn=make_embed_fn({}),
                        index=_LinearIndex(DIM))

    r1 = store.ingest_daily_logs(str(tmp_path))
    r2 = store.ingest_daily_logs(str(tmp_path))  # same file, same content

    assert r1["chunks_upserted"] >= 2
    assert r2["chunks_upserted"] == 0
    assert r2["chunks_skipped"] >= 1


def test_ingest_daily_logs_discards_short_chunks(tmp_path):
    """Chunks shorter than 20 chars are discarded."""
    mem_dir = tmp_path / "data" / "memory"
    mem_dir.mkdir(parents=True)
    (mem_dir / "2026-03-02.md").write_text("Ok.\n\nLong enough paragraph text here.\n")

    store = MemoryStore(data_dir=str(tmp_path),
                        embed_fn=make_embed_fn({}),
                        index=_LinearIndex(DIM))
    result = store.ingest_daily_logs(str(tmp_path))

    # Only the long chunk should be upserted
    assert result["chunks_upserted"] == 1


# ---------------------------------------------------------------------------
# Test: ingest_transcripts
# ---------------------------------------------------------------------------

def _write_jsonl(path, lines):
    with open(path, "w") as f:
        for obj in lines:
            f.write(json.dumps(obj) + "\n")


def test_ingest_transcripts_basic(tmp_path):
    """ingest_transcripts pairs inbound+outbound turns into chunks."""
    tx_dir = tmp_path / "data" / "transcripts"
    tx_dir.mkdir(parents=True)

    ts = "2026-03-01T10:00:00Z"
    _write_jsonl(tx_dir / "2026-03-01.jsonl", [
        {"direction": "inbound",  "text": "What time is it?",    "ts": ts},
        {"direction": "outbound", "text": "It is 10 AM.",        "ts": ts},
        {"direction": "inbound",  "text": "Thanks!",             "ts": ts},
        {"direction": "outbound", "text": "You're welcome.",     "ts": ts},
    ])

    store = MemoryStore(data_dir=str(tmp_path),
                        embed_fn=make_embed_fn({}),
                        index=_LinearIndex(DIM))
    result = store.ingest_transcripts(str(tmp_path))

    assert result["files_scanned"] == 1
    assert result["chunks_upserted"] == 2  # two Q/A pairs


def test_ingest_transcripts_advances_cursor(tmp_path):
    """Second ingest of same file with new lines only processes new lines."""
    tx_dir = tmp_path / "data" / "transcripts"
    tx_dir.mkdir(parents=True)
    txfile = tx_dir / "2026-03-01.jsonl"

    ts = "2026-03-01T10:00:00Z"
    lines = [
        {"direction": "inbound",  "text": "Hello?",  "ts": ts},
        {"direction": "outbound", "text": "Hi!",     "ts": ts},
    ]
    _write_jsonl(txfile, lines)

    store = MemoryStore(data_dir=str(tmp_path),
                        embed_fn=make_embed_fn({}),
                        index=_LinearIndex(DIM))

    r1 = store.ingest_transcripts(str(tmp_path))
    assert r1["chunks_upserted"] == 1

    # Append new turn pair to same file
    with open(txfile, "a") as f:
        f.write(json.dumps({"direction": "inbound",  "text": "New msg?", "ts": ts}) + "\n")
        f.write(json.dumps({"direction": "outbound", "text": "Yes!",     "ts": ts}) + "\n")

    r2 = store.ingest_transcripts(str(tmp_path))
    assert r2["chunks_upserted"] == 1  # only the new pair
    assert r2["chunks_skipped"] == 0


# ---------------------------------------------------------------------------
# Test: _tokenize helper
# ---------------------------------------------------------------------------

def test_tokenize_lowercases_and_splits():
    tokens = _tokenize("Hello World")
    assert "hello" in tokens
    assert "world" in tokens


def test_tokenize_removes_short_words():
    tokens = _tokenize("a be the word")
    assert all(len(t) >= 2 for t in tokens)
