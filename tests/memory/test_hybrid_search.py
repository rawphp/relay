import time
from typing import Dict

import numpy as np
import faiss
import pytest

from lib.memory.hybrid_search import hybrid_search


class DummyModel:
    def __init__(self, embedding_map: Dict[str, np.ndarray]):
        self.embedding_map = embedding_map

    def encode(self, text: str) -> np.ndarray:
        if text not in self.embedding_map:
            raise KeyError(f"No embedding for {text}")
        return self.embedding_map[text]


def setup_fake_index(monkeypatch, chunks, vectors, query_vec, query_text):
    dim = vectors.shape[1]
    index = faiss.IndexFlatIP(dim)
    index.add(vectors.astype("float32"))

    metadata = {
        "model": "dummy",
        "chunks": chunks,
        "dimension": dim,
        "total_vectors": len(chunks),
    }

    def fake_load_index():
        return index, metadata

    dummy_model = DummyModel({query_text: query_vec.astype("float32")})

    monkeypatch.setattr("lib.memory.search.load_index", fake_load_index)
    monkeypatch.setattr("lib.memory.hybrid_search.get_model", lambda _name: dummy_model)


@pytest.fixture
def base_time():
    return time.time()


def test_keyword_boost_ranks_document_higher(monkeypatch, base_time):
    chunks = [
        {
            "text": "Document handling via Telegram finished today",
            "file": "2026-02-18.md",
            "line_start": 10,
            "line_end": 14,
            "file_importance": 0.5,
            "file_modified_at": base_time,
        },
        {
            "text": "Unrelated deployment troubleshooting log",
            "file": "2026-02-17.md",
            "line_start": 30,
            "line_end": 35,
            "file_importance": 0.5,
            "file_modified_at": base_time,
        },
    ]

    vectors = np.array([
        [0.9, 0.1, 0.0, 0.0],
        [0.4, 0.6, 0.0, 0.0],
    ], dtype="float32")
    query_vec = np.array([0.95, 0.05, 0.0, 0.0], dtype="float32")

    query_text = "document telegram"
    setup_fake_index(monkeypatch, chunks, vectors, query_vec, query_text)

    results = hybrid_search(query_text, top_k=2, min_score=0.1)

    assert len(results) == 2
    assert results[0]["file"] == "2026-02-18.md"
    assert results[0]["score"] > results[1]["score"]
    assert results[0]["keyword_score"] > results[1]["keyword_score"]


def test_importance_multiplier_raises_high_priority_result(monkeypatch, base_time):
    chunks = [
        {
            "text": "Semantic memory overview and priorities",
            "file": "priority_low.md",
            "line_start": 5,
            "line_end": 9,
            "file_importance": 0.1,
            "file_modified_at": base_time,
        },
        {
            "text": "Semantic memory overview and priorities",
            "file": "priority_high.md",
            "line_start": 15,
            "line_end": 19,
            "file_importance": 0.9,
            "file_modified_at": base_time,
        },
    ]

    vectors = np.array([
        [0.8, 0.2, 0.0, 0.0],
        [0.8, 0.2, 0.0, 0.0],
    ], dtype="float32")
    query_vec = np.array([0.8, 0.2, 0.0, 0.0], dtype="float32")

    setup_fake_index(monkeypatch, chunks, vectors, query_vec, "importance query")

    results = hybrid_search("importance query", top_k=2, min_score=0.1)

    assert results[0]["file"] == "priority_high.md"
    assert results[0]["score"] > results[1]["score"]


def test_recency_decay_penalizes_old_chunks(monkeypatch, base_time):
    chunks = [
        {
            "text": "Latest semantic memory learning",
            "file": "fresh.md",
            "line_start": 1,
            "line_end": 4,
            "file_importance": 0.5,
            "file_modified_at": base_time,
        },
        {
            "text": "Old notes from months ago",
            "file": "stale.md",
            "line_start": 10,
            "line_end": 12,
            "file_importance": 0.5,
            "file_modified_at": base_time - 120 * 86400,
        },
    ]

    vectors = np.array([
        [0.6, 0.4, 0.0, 0.0],
        [0.6, 0.4, 0.0, 0.0],
    ], dtype="float32")
    query_vec = np.array([0.6, 0.4, 0.0, 0.0], dtype="float32")

    setup_fake_index(monkeypatch, chunks, vectors, query_vec, "fresh notes")

    results = hybrid_search("fresh notes", top_k=2, min_score=0.1)

    assert results[0]["file"] == "fresh.md"
    assert results[0]["score"] > results[1]["score"]
    assert results[1]["days_old"] >= 119


def test_dedup_and_token_budget(monkeypatch, base_time):
    chunks = [
        {
            "text": "First chunk from same file",
            "file": "log.md",
            "line_start": 1,
            "line_end": 2,
            "file_importance": 0.5,
            "file_modified_at": base_time,
        },
        {
            "text": "Second chunk from same file",
            "file": "log.md",
            "line_start": 4,
            "line_end": 5,
            "file_importance": 0.5,
            "file_modified_at": base_time,
        },
        {
            "text": "Small chunk from another file",
            "file": "extra.md",
            "line_start": 1,
            "line_end": 1,
            "file_importance": 0.5,
            "file_modified_at": base_time,
        },
    ]

    vectors = np.array([
        [0.7, 0.3, 0.0, 0.0],
        [0.9, 0.1, 0.0, 0.0],
        [0.6, 0.4, 0.0, 0.0],
    ], dtype="float32")
    query_vec = np.array([0.8, 0.2, 0.0, 0.0], dtype="float32")

    setup_fake_index(monkeypatch, chunks, vectors, query_vec, "log summary")

    results = hybrid_search(
        "log summary",
        top_k=5,
        min_score=0.1,
        token_budget=5,  # Forces dropping once 20 chars consumed
    )

    assert len(results) <= 2
    files = {res["file"] for res in results}
    assert len(files) == len(results)


def test_keyword_fallback_when_vector_misses(monkeypatch, base_time):
    """When vector search returns low scores (all filtered), keyword results
    should still surface via the fallback path."""
    from unittest.mock import patch

    # Two chunks: one contains the search keyword, one is unrelated
    chunks = [
        {
            "text": "djb2 hash implementation for dedup ring",
            "file": "notes.md",
            "line_start": 1,
            "line_end": 3,
            "file_importance": 0.5,
            "file_modified_at": base_time,
        },
        {
            "text": "completely unrelated content about weather",
            "file": "other.md",
            "line_start": 1,
            "line_end": 3,
            "file_importance": 0.5,
            "file_modified_at": base_time,
        },
    ]

    # Vector embeddings — query is orthogonal to both chunks (low similarity)
    dim = 4
    vectors = np.array([
        [0.1, 0.1, 0.9, 0.0],   # notes.md — low similarity to query
        [0.0, 0.1, 0.0, 0.9],   # other.md — low similarity
    ], dtype="float32")
    query_vec = np.array([0.0, 0.0, 0.0, 0.0], dtype="float32")  # zero vec → all scores ~0

    setup_fake_index(monkeypatch, chunks, vectors, query_vec, "djb2")

    results = hybrid_search(
        "djb2",
        top_k=3,
        min_score=0.5,  # High threshold that vector results won't pass
        include_context=False,
        recency_boost=False,
        importance_boost=False,
    )

    # Keyword fallback should find the chunk containing "djb2"
    assert len(results) >= 1, "Keyword fallback should return at least one result"
    texts = [r["text"] for r in results]
    assert any("djb2" in t for t in texts), "Should find the djb2 chunk via keyword fallback"


# ── Source label tests ─────────────────────────────────────────────────────────

def test_source_field_defaults_to_agent_when_absent(monkeypatch, base_time):
    """Chunks without a source field should return source='agent' in results."""
    chunks = [
        {
            "text": "The agent implemented async heartbeat dispatch",
            "file": "2026-03-01.md",
            "line_start": 1,
            "line_end": 2,
            "file_importance": 0.5,
            "file_modified_at": base_time,
            # no 'source' key
        },
    ]
    vectors = np.array([[0.9, 0.1, 0.0, 0.0]], dtype="float32")
    query_vec = np.array([0.9, 0.1, 0.0, 0.0], dtype="float32")
    setup_fake_index(monkeypatch, chunks, vectors, query_vec, "heartbeat")
    results = hybrid_search("heartbeat", top_k=1, min_score=0.0,
                            recency_boost=False, importance_boost=False)
    assert len(results) == 1
    assert results[0]["source"] == "agent"


def test_source_field_user_propagated_to_results(monkeypatch, base_time):
    """Chunks tagged source='user' should appear as source='user' in results."""
    chunks = [
        {
            "text": "The user likes Roblox and PS5 games",
            "file": "USER.md",
            "line_start": 5,
            "line_end": 6,
            "file_importance": 0.7,
            "file_modified_at": base_time,
            "source": "user",
        },
    ]
    vectors = np.array([[0.9, 0.1, 0.0, 0.0]], dtype="float32")
    query_vec = np.array([0.9, 0.1, 0.0, 0.0], dtype="float32")
    setup_fake_index(monkeypatch, chunks, vectors, query_vec, "user")
    results = hybrid_search("user", top_k=1, min_score=0.0,
                            recency_boost=False, importance_boost=False)
    assert len(results) == 1
    assert results[0]["source"] == "user"


def test_source_field_agent_propagated_to_results(monkeypatch, base_time):
    """Chunks tagged source='agent' should appear as source='agent' in results."""
    chunks = [
        {
            "text": "The agent implemented crash recovery feature",
            "file": "2026-03-01.md",
            "line_start": 10,
            "line_end": 11,
            "file_importance": 0.5,
            "file_modified_at": base_time,
            "source": "agent",
        },
    ]
    vectors = np.array([[0.9, 0.1, 0.0, 0.0]], dtype="float32")
    query_vec = np.array([0.9, 0.1, 0.0, 0.0], dtype="float32")
    setup_fake_index(monkeypatch, chunks, vectors, query_vec, "crash recovery")
    results = hybrid_search("crash recovery", top_k=1, min_score=0.0,
                            recency_boost=False, importance_boost=False)
    assert len(results) == 1
    assert results[0]["source"] == "agent"


# ── infer_source tests ─────────────────────────────────────────────────────────

from pathlib import Path
from lib.memory.build_index import infer_source


def test_infer_source_user_md_returns_user():
    assert infer_source(Path("/some/path/USER.md")) == "user"


def test_infer_source_priorities_md_returns_user():
    assert infer_source(Path("/some/path/PRIORITIES.md")) == "user"


def test_infer_source_daily_file_returns_agent():
    assert infer_source(Path("/some/path/2026-03-01.md")) == "agent"


def test_infer_source_memory_md_returns_agent():
    assert infer_source(Path("/some/path/MEMORY.md")) == "agent"


def test_infer_source_soul_md_returns_agent():
    assert infer_source(Path("/some/path/SOUL.md")) == "agent"


def test_infer_source_unknown_file_returns_agent():
    assert infer_source(Path("/some/path/unknown_notes.md")) == "agent"
