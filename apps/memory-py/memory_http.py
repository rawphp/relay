#!/usr/bin/env python3
"""
memory_http.py — Relay memory HTTP sidecar.

Self-contained Python HTTP service that owns FAISS+BM25 indexes, serves the
unified memory API on localhost:8765, and handles search, upsert, delete,
and ingestion operations.

Usage:
    python3 memory_http.py --data-dir ~/relay/data --port 8765

Design:
    MemoryStore — pure business logic; accepts embed_fn and index as deps.
    _LinearIndex — numpy-only fallback index used in tests (no faiss required).
    HTTP layer — wraps MemoryStore; serves JSON on localhost.
"""

import hashlib
import json
import math
import os
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from typing import Callable, Dict, List, Optional, Tuple

import numpy as np


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

DEFAULT_PORT      = 8765
DEFAULT_DATA_DIR  = os.path.join(os.path.expanduser("~"), "relay", "data")
MODEL_NAME        = "BAAI/bge-small-en-v1.5"
EMBEDDING_DIM     = 384
HALF_LIFE_DAYS    = 30
_RECENCY_K        = math.log(2) / HALF_LIFE_DAYS

# BM25 tuning
_BM25_K1 = 1.5
_BM25_B  = 0.75

# RRF constant
_RRF_K = 60

# Chunk min length
MIN_CHUNK_LEN = 20

# Max Q+A pair length for transcript chunks
MAX_QA_LEN = 400

# Cursor file name
CURSOR_FILE = ".ingest_cursor.json"

# FAISS index sub-path (relative to data_dir)
FAISS_INDEX_PATH = os.path.join("memory", ".index", "faiss.index")
STORE_META_PATH  = os.path.join("memory", ".index", "store_meta.json")


# ---------------------------------------------------------------------------
# Tokenizer (shared by BM25 indexer and query parser)
# ---------------------------------------------------------------------------

_STOP_WORDS = frozenset({
    "a", "an", "the", "and", "or", "but", "in", "on", "at", "to", "for",
    "of", "with", "by", "from", "as", "is", "was", "are", "were", "been",
    "be", "have", "has", "had", "do", "does", "did", "will", "would",
    "should", "could", "may", "might", "must", "can", "this", "that",
    "these", "those", "i", "you", "he", "she", "it", "we", "they",
    "what", "which", "who", "when", "where", "why", "how", "so", "not",
    "no", "if", "then", "its", "my", "your",
})


def _tokenize(text: str) -> List[str]:
    """Lower-case, split on non-alphanumeric, drop stop-words and short tokens."""
    words = re.findall(r"\b\w+\b", text.lower())
    return [w for w in words if len(w) >= 2 and w not in _STOP_WORDS]


# ---------------------------------------------------------------------------
# _LinearIndex — numpy-only vector index (used in tests; no faiss needed)
# ---------------------------------------------------------------------------

class _LinearIndex:
    """
    Simple linear-scan vector store.  Duck-type-compatible with the faiss
    wrapper used in production.  All vectors are kept in-memory.
    """

    def __init__(self, dim: int):
        self._dim = dim
        self._fids: List[int]         = []   # faiss integer IDs
        self._vecs: List[np.ndarray]  = []   # corresponding unit vectors

    # -- write ops -----------------------------------------------------------

    def add(self, vec: np.ndarray, fid: int) -> None:
        """Add a single vector with an integer ID."""
        self._fids.append(int(fid))
        self._vecs.append(vec.astype("float32"))

    def remove(self, fid: int) -> None:
        """Remove the vector with the given integer ID (no-op if missing)."""
        if fid in self._fids:
            i = self._fids.index(fid)
            self._fids.pop(i)
            self._vecs.pop(i)

    # -- read ops ------------------------------------------------------------

    def search(self, query: np.ndarray, k: int) -> Tuple[List[float], List[int]]:
        """Return (scores, fids) of the top-k closest vectors."""
        if not self._vecs:
            return [], []
        mat = np.array(self._vecs, dtype="float32")
        scores = mat @ query.astype("float32")
        k = min(k, len(scores))
        top_idx = np.argsort(-scores)[:k]
        return [float(scores[i]) for i in top_idx], [self._fids[i] for i in top_idx]

    def size(self) -> int:
        return len(self._fids)

    # -- persistence (no-op for test index) ----------------------------------

    def save(self, path: str) -> None:
        pass

    @classmethod
    def load(cls, path: str, dim: int) -> "_LinearIndex":
        return cls(dim)


# ---------------------------------------------------------------------------
# _FaissIndex — thin wrapper around faiss.IndexIDMap2
# ---------------------------------------------------------------------------

class _FaissIndex:
    """Production vector index backed by faiss.IndexIDMap2."""

    def __init__(self, dim: int, index=None):
        import faiss
        if index is not None:
            self._index = index
        else:
            flat = faiss.IndexFlatIP(dim)
            self._index = faiss.IndexIDMap2(flat)
        self._dim = dim

    def add(self, vec: np.ndarray, fid: int) -> None:
        import faiss
        v = vec.reshape(1, -1).astype("float32")
        ids = np.array([fid], dtype="int64")
        self._index.add_with_ids(v, ids)

    def remove(self, fid: int) -> None:
        import faiss
        sel = faiss.IDSelectorBatch(np.array([fid], dtype="int64"))
        self._index.remove_ids(sel)

    def search(self, query: np.ndarray, k: int) -> Tuple[List[float], List[int]]:
        if self._index.ntotal == 0:
            return [], []
        q = query.reshape(1, -1).astype("float32")
        k = min(k, self._index.ntotal)
        scores, ids = self._index.search(q, k)
        return [float(s) for s in scores[0]], [int(i) for i in ids[0]]

    def size(self) -> int:
        return self._index.ntotal

    def save(self, path: str) -> None:
        import faiss
        os.makedirs(os.path.dirname(path), exist_ok=True)
        faiss.write_index(self._index, path)

    @classmethod
    def load(cls, path: str, dim: int) -> "_FaissIndex":
        import faiss
        index = faiss.read_index(path)
        return cls(dim, index)


# ---------------------------------------------------------------------------
# MemoryStore
# ---------------------------------------------------------------------------

class MemoryStore:
    """
    Core memory store: FAISS/linear vector search + BM25 keyword search,
    fused with RRF.  All heavy deps are injected so tests can run without
    sentence-transformers or faiss installed.

    Parameters
    ----------
    data_dir  : Base data directory (used for cursor and index persistence).
                May be None when data_dir operations are not exercised (tests).
    embed_fn  : Callable text -> np.ndarray (unit vector, shape=(dim,)).
                If None, the BGE-small model is lazy-loaded on first call.
    index     : Duck-typed vector index.  If None, a _FaissIndex is used.
    """

    def __init__(
        self,
        data_dir: Optional[str],
        embed_fn: Optional[Callable[[str], np.ndarray]] = None,
        index=None,
    ):
        self._data_dir = data_dir
        self._embed_fn = embed_fn  # may stay None until first use
        self._index = index        # same

        # Memory records: id -> {text, metadata, upserted_at, importance}
        self._memories: Dict[str, dict] = {}

        # Faiss integer ID management
        self._next_fid: int = 0
        self._id_to_fid: Dict[str, int] = {}  # memory_id -> faiss_int_id
        self._fid_to_id: Dict[int, str] = {}  # faiss_int_id -> memory_id

        # BM25 structures
        self._inverted: Dict[str, Dict[str, int]] = {}  # term -> {id: tf}
        self._doc_lengths: Dict[str, int] = {}          # id -> token count

    # -----------------------------------------------------------------------
    # Lazy-init helpers
    # -----------------------------------------------------------------------

    def _get_embed_fn(self) -> Callable[[str], np.ndarray]:
        if self._embed_fn is None:
            from sentence_transformers import SentenceTransformer
            _model = SentenceTransformer(MODEL_NAME)
            self._embed_fn = lambda t: _model.encode(
                t, normalize_embeddings=True
            )
        return self._embed_fn

    def _get_index(self):
        if self._index is None:
            if self._data_dir:
                idx_path = os.path.join(self._data_dir, FAISS_INDEX_PATH)
                if os.path.exists(idx_path):
                    self._index = _FaissIndex.load(idx_path, EMBEDDING_DIM)
                    return self._index
            self._index = _FaissIndex(EMBEDDING_DIM)
        return self._index

    # -----------------------------------------------------------------------
    # BM25 helpers
    # -----------------------------------------------------------------------

    def _bm25_index_doc(self, doc_id: str, text: str) -> None:
        tokens = _tokenize(text)
        self._doc_lengths[doc_id] = len(tokens)
        tf: Dict[str, int] = {}
        for t in tokens:
            tf[t] = tf.get(t, 0) + 1
        for term, freq in tf.items():
            if term not in self._inverted:
                self._inverted[term] = {}
            self._inverted[term][doc_id] = freq

    def _bm25_remove_doc(self, doc_id: str, text: str) -> None:
        tokens = set(_tokenize(text))
        for term in tokens:
            if term in self._inverted:
                self._inverted[term].pop(doc_id, None)
        self._doc_lengths.pop(doc_id, None)

    def _bm25_avg_dl(self) -> float:
        if not self._doc_lengths:
            return 1.0
        return sum(self._doc_lengths.values()) / len(self._doc_lengths)

    def _bm25_score(self, query_terms: List[str], doc_id: str) -> float:
        dl = self._doc_lengths.get(doc_id, 0)
        avg_dl = self._bm25_avg_dl()
        N = max(len(self._memories), 1)
        score = 0.0
        for term in query_terms:
            tf = self._inverted.get(term, {}).get(doc_id, 0)
            if tf == 0:
                continue
            n = len(self._inverted.get(term, {}))
            idf = math.log((N - n + 0.5) / (n + 0.5) + 1)
            score += idf * (tf * (_BM25_K1 + 1)) / (
                tf + _BM25_K1 * (1 - _BM25_B + _BM25_B * dl / avg_dl)
            )
        return score

    # -----------------------------------------------------------------------
    # Public API
    # -----------------------------------------------------------------------

    def upsert(self, doc_id: str, text: str, metadata: dict) -> None:
        """Insert or replace a memory."""
        # Remove old entry if updating
        if doc_id in self._memories:
            old_text = self._memories[doc_id]["text"]
            self._bm25_remove_doc(doc_id, old_text)
            old_fid = self._id_to_fid.pop(doc_id, None)
            if old_fid is not None:
                self._fid_to_id.pop(old_fid, None)
                self._get_index().remove(old_fid)

        importance = float(metadata.get("importance", 0.5))
        self._memories[doc_id] = {
            "text": text,
            "metadata": metadata,
            "upserted_at": time.time(),
            "importance": importance,
        }

        # Vector index
        vec = self._get_embed_fn()(text)
        fid = self._next_fid
        self._next_fid += 1
        self._id_to_fid[doc_id] = fid
        self._fid_to_id[fid] = doc_id
        self._get_index().add(vec, fid)

        # BM25 index
        self._bm25_index_doc(doc_id, text)

    def delete(self, doc_id: str) -> bool:
        """Remove a memory.  Returns True if found and deleted."""
        if doc_id not in self._memories:
            return False
        old_text = self._memories[doc_id]["text"]
        self._bm25_remove_doc(doc_id, old_text)
        fid = self._id_to_fid.pop(doc_id, None)
        if fid is not None:
            self._fid_to_id.pop(fid, None)
            self._get_index().remove(fid)
        del self._memories[doc_id]
        return True

    def search(self, query: str, top_k: int = 5,
               min_score: float = 0.0) -> List[dict]:
        """
        Hybrid search: FAISS vector + BM25, fused with RRF.
        Final score = rrf_score * recency_factor * importance.
        """
        if not self._memories:
            return []

        embed_fn = self._get_embed_fn()
        index    = self._get_index()
        n        = len(self._memories)

        # --- Vector search ---
        query_vec = embed_fn(query)
        v_scores, v_fids = index.search(query_vec, min(n, 50))
        v_rank: Dict[str, int] = {}
        for rank, fid in enumerate(v_fids):
            mem_id = self._fid_to_id.get(fid)
            if mem_id is not None:
                v_rank[mem_id] = rank  # lower rank = better

        # --- BM25 search ---
        query_terms = _tokenize(query)
        bm25_raw: Dict[str, float] = {}
        if query_terms:
            for mid in self._memories:
                s = self._bm25_score(query_terms, mid)
                if s > 0:
                    bm25_raw[mid] = s

        # Sort BM25 by descending score to get ranks
        bm25_ranked = sorted(bm25_raw.keys(), key=lambda m: -bm25_raw[m])
        bm25_rank: Dict[str, int] = {mid: r for r, mid in enumerate(bm25_ranked)}

        # --- Determine strategy ---
        has_vector = bool(v_rank)
        has_bm25   = bool(bm25_rank)
        if has_vector and has_bm25:
            strategy = "hybrid"
        elif has_bm25:
            strategy = "bm25_only"
        else:
            strategy = "vector_only"

        # --- Fallback: BM25-only when vector index cold ---
        candidates = set(v_rank.keys()) | set(bm25_rank.keys())
        if not candidates:
            return []

        # --- RRF fusion ---
        now = time.time()
        results = []
        for mid in candidates:
            vr = v_rank.get(mid, n + _RRF_K)  # penalise missing
            br = bm25_rank.get(mid, n + _RRF_K)
            rrf = 1.0 / (vr + _RRF_K) + 1.0 / (br + _RRF_K)

            mem = self._memories[mid]
            age_days = (now - mem["upserted_at"]) / 86400.0
            recency  = math.exp(-_RECENCY_K * age_days)
            imp      = mem["importance"]

            final_score = rrf * recency * imp

            results.append({
                "id":         mid,
                "text":       mem["text"],
                "metadata":   mem["metadata"],
                "score":      final_score,
                "strategy":   strategy,
                "upserted_at": mem["upserted_at"],
                "importance": imp,
            })

        results.sort(key=lambda r: -r["score"])
        results = [r for r in results if r["score"] >= min_score]
        return results[:top_k]

    def health(self) -> dict:
        try:
            index = self._get_index()
            size = index.size()
            # Deep check: run a test query if the index has data
            if size > 0:
                probe_vec = self._get_embed_fn()("health check probe")
                index.search(probe_vec, 1)
            return {"status": "ok", "index_size": size}
        except Exception as exc:
            return {"status": "error", "detail": str(exc)}

    # -----------------------------------------------------------------------
    # Ingestion
    # -----------------------------------------------------------------------

    def _cursor_path(self, agent_home: str) -> str:
        return os.path.join(agent_home, "data", "memory", CURSOR_FILE)

    def _load_cursor(self, agent_home: str) -> dict:
        path = self._cursor_path(agent_home)
        if os.path.exists(path):
            try:
                with open(path) as f:
                    return json.load(f)
            except (json.JSONDecodeError, OSError):
                pass
        return {}

    def _save_cursor(self, agent_home: str, cursor: dict) -> None:
        path = self._cursor_path(agent_home)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            json.dump(cursor, f)

    def ingest_daily_logs(self, agent_home: str) -> dict:
        """
        Scan {agent_home}/data/memory/*.md, split into paragraph chunks,
        upsert new/changed content.  Skip unchanged files via SHA-256 cursor.
        """
        cursor = self._load_cursor(agent_home)
        mem_dir = os.path.join(agent_home, "data", "memory")
        files_scanned = 0
        chunks_upserted = 0
        chunks_skipped = 0

        if not os.path.isdir(mem_dir):
            self._save_cursor(agent_home, cursor)
            return {
                "files_scanned":   0,
                "chunks_upserted": 0,
                "chunks_skipped":  0,
            }

        for fname in sorted(os.listdir(mem_dir)):
            if not fname.endswith(".md"):
                continue
            fpath = os.path.join(mem_dir, fname)
            files_scanned += 1

            with open(fpath, "rb") as f:
                raw = f.read()
            file_sha = hashlib.sha256(raw).hexdigest()

            cursor_key = "log:" + fname
            if cursor.get(cursor_key) == file_sha:
                chunks_skipped += 1
                continue  # unchanged

            text = raw.decode("utf-8", errors="replace")
            paragraphs = [p.strip() for p in text.split("\n\n")]

            for para in paragraphs:
                if len(para) < MIN_CHUNK_LEN:
                    continue
                mem_id = hashlib.sha256(
                    (fname + para).encode()
                ).hexdigest()[:16]
                self.upsert(mem_id, para, {
                    "source":     "daily_log",
                    "file":       fname,
                    "importance": 0.5,
                })
                chunks_upserted += 1

            cursor[cursor_key] = file_sha

        self._save_cursor(agent_home, cursor)
        return {
            "files_scanned":   files_scanned,
            "chunks_upserted": chunks_upserted,
            "chunks_skipped":  chunks_skipped,
        }

    def ingest_transcripts(self, agent_home: str) -> dict:
        """
        Scan {agent_home}/data/transcripts/*.jsonl, pair consecutive
        (inbound, outbound) turns into "Q: …\\nA: …" chunks (max 400 chars).
        Tracks processed line counts per file in the shared cursor.
        """
        cursor = self._load_cursor(agent_home)
        tx_dir = os.path.join(agent_home, "data", "transcripts")
        files_scanned = 0
        chunks_upserted = 0
        chunks_skipped = 0

        if not os.path.isdir(tx_dir):
            self._save_cursor(agent_home, cursor)
            return {
                "files_scanned":   0,
                "chunks_upserted": 0,
                "chunks_skipped":  0,
            }

        for fname in sorted(os.listdir(tx_dir)):
            if not fname.endswith(".jsonl"):
                continue
            fpath = os.path.join(tx_dir, fname)
            files_scanned += 1

            cursor_key = "tx:" + fname
            prev_line_count = cursor.get(cursor_key, 0)

            lines = []
            with open(fpath) as f:
                for _ in range(prev_line_count):
                    f.readline()  # skip already-processed lines
                for raw_line in f:
                    raw_line = raw_line.strip()
                    if raw_line:
                        lines.append(raw_line)

            new_line_count = prev_line_count + len(lines)
            cursor[cursor_key] = new_line_count

            # Parse JSON lines
            records = []
            for raw_line in lines:
                try:
                    records.append(json.loads(raw_line))
                except json.JSONDecodeError:
                    continue

            # Pair consecutive inbound/outbound turns
            i = 0
            while i < len(records) - 1:
                a, b = records[i], records[i + 1]
                if a.get("direction") == "inbound" and b.get("direction") == "outbound":
                    qa = f"Q: {a.get('text', '')}\nA: {b.get('text', '')}"
                    qa = qa[:MAX_QA_LEN]
                    ts = a.get("ts", "")
                    mem_id = hashlib.sha256(ts.encode()).hexdigest()[:16]
                    self.upsert(mem_id, qa, {
                        "source":     "transcript",
                        "ts":         ts,
                        "importance": 0.3,
                    })
                    chunks_upserted += 1
                    i += 2
                else:
                    i += 1

        self._save_cursor(agent_home, cursor)
        return {
            "files_scanned":   files_scanned,
            "chunks_upserted": chunks_upserted,
            "chunks_skipped":  chunks_skipped,
        }


# ---------------------------------------------------------------------------
# HTTP server
# ---------------------------------------------------------------------------

def _json_body(handler: BaseHTTPRequestHandler) -> dict:
    length = int(handler.headers.get("Content-Length", 0))
    raw = handler.rfile.read(length) if length else b""
    return json.loads(raw) if raw else {}


def _respond(handler: BaseHTTPRequestHandler, code: int, body: dict) -> None:
    payload = json.dumps(body).encode()
    handler.send_response(code)
    handler.send_header("Content-Type", "application/json")
    handler.send_header("Content-Length", str(len(payload)))
    handler.end_headers()
    handler.wfile.write(payload)


class _MemoryHandler(BaseHTTPRequestHandler):
    """HTTP request handler — uses the module-level _store singleton."""

    store: "MemoryStore"  # set by make_handler()
    auth_token: Optional[str] = None  # set by make_handler()

    def log_message(self, fmt, *args):  # suppress default Apache-style log
        pass

    def _check_auth(self) -> bool:
        """Validate auth token from ?auth= query parameter. Returns True if OK."""
        if not self.auth_token:
            return True  # no auth configured
        from urllib.parse import urlparse, parse_qs
        parsed = urlparse(self.path)
        params = parse_qs(parsed.query)
        token = params.get("auth", [None])[0]
        if token != self.auth_token:
            _respond(self, 401, {"error": "unauthorized"})
            return False
        # Strip auth param from path for routing
        self.path = parsed.path
        return True

    def do_GET(self):
        if not self._check_auth():
            return
        if self.path == "/health":
            result = self.store.health()
            code = 200 if result.get("status") == "ok" else 503
            _respond(self, code, result)
        else:
            _respond(self, 404, {"error": "not found"})

    def do_POST(self):
        if not self._check_auth():
            return
        try:
            body = _json_body(self)
        except (json.JSONDecodeError, ValueError) as exc:
            _respond(self, 400, {"error": str(exc)})
            return

        if self.path == "/search":
            results = self.store.search(
                query    = body.get("query", ""),
                top_k    = int(body.get("top_k", 5)),
                min_score= float(body.get("min_score", 0.0)),
            )
            _respond(self, 200, {"results": results})

        elif self.path == "/upsert":
            self.store.upsert(
                doc_id   = body["id"],
                text     = body["text"],
                metadata = body.get("metadata", {}),
            )
            _respond(self, 200, {"id": body["id"]})

        elif self.path == "/delete":
            deleted = self.store.delete(body["id"])
            _respond(self, 200, {"deleted": deleted})

        elif self.path == "/ingest_daily_logs":
            agent_id = body.get("agent_id", "")
            agent_home = _resolve_agent_home(agent_id)
            result = self.store.ingest_daily_logs(agent_home)
            _respond(self, 200, result)

        elif self.path == "/ingest_transcripts":
            agent_id = body.get("agent_id", "")
            agent_home = _resolve_agent_home(agent_id)
            result = self.store.ingest_transcripts(agent_home)
            _respond(self, 200, result)

        else:
            _respond(self, 404, {"error": "not found"})


def _resolve_agent_home(agent_id: str) -> str:
    """Map agent_id to home directory.  Falls back to ~/relay."""
    registry = os.path.expanduser("~/.relay")
    if os.path.isfile(registry):
        with open(registry) as f:
            for line in f:
                line = line.strip()
                if "=" in line:
                    name, path = line.split("=", 1)
                    if name.strip() == agent_id:
                        return os.path.expanduser(path.strip())
    return os.path.expanduser("~/relay")


def make_handler(store: MemoryStore, auth_token: Optional[str] = None):
    """Return an HTTP handler class bound to *store*."""
    class Handler(_MemoryHandler):
        pass
    Handler.store = store
    Handler.auth_token = auth_token
    return Handler


def create_production_store(data_dir: str) -> MemoryStore:
    """Factory for production use: loads real model and faiss index."""
    store = MemoryStore(data_dir=data_dir)
    # Trigger lazy init so startup errors surface early
    _ = store._get_embed_fn()
    _ = store._get_index()
    return store


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Relay memory HTTP sidecar")
    parser.add_argument("--data-dir", default=DEFAULT_DATA_DIR,
                        help="Base data directory (default: ~/relay/data)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help=f"Port to listen on (default: {DEFAULT_PORT})")
    parser.add_argument("--auth-token", default=None,
                        help="Bearer auth token (reject requests without it)")
    args = parser.parse_args()

    data_dir = os.path.expanduser(args.data_dir)
    print(f"[memory_http] Starting — data_dir={data_dir} port={args.port}",
          flush=True)

    store = create_production_store(data_dir)
    handler = make_handler(store, auth_token=args.auth_token)
    httpd = HTTPServer(("127.0.0.1", args.port), handler)

    print(f"[memory_http] Listening on 127.0.0.1:{args.port}", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    print("[memory_http] Stopped.", flush=True)


if __name__ == "__main__":
    main()
