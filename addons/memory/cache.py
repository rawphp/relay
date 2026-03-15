"""
Embedding cache manager for avoiding re-embedding unchanged chunks.

Uses SHA-256 hash of text as cache key, stores embeddings in JSON file.
"""

import os
import json
import hashlib
from pathlib import Path
from typing import Optional, Dict, List
import numpy as np


class EmbeddingCache:
    """Persistent cache for text embeddings."""

    def __init__(self, cache_path: Optional[str] = None):
        """
        Initialize embedding cache.

        Args:
            cache_path: Path to cache file (default: data/memory/.cache/embeddings.json)
        """
        if cache_path is None:
            relay_root = Path(os.environ.get("RELAY_HOME", os.path.expanduser("~/relay")))
            cache_dir = relay_root / "data" / "memory" / ".cache"
            cache_dir.mkdir(parents=True, exist_ok=True)
            cache_path = str(cache_dir / "embeddings.json")

        self.cache_path = cache_path
        self.cache: Dict[str, List[float]] = {}
        self.hits = 0
        self.misses = 0
        self._load()

    def _load(self):
        """Load cache from disk."""
        if os.path.exists(self.cache_path):
            try:
                with open(self.cache_path, 'r') as f:
                    self.cache = json.load(f)
            except (json.JSONDecodeError, IOError):
                # Corrupted cache, start fresh
                self.cache = {}

    def _save(self):
        """Save cache to disk."""
        try:
            with open(self.cache_path, 'w') as f:
                json.dump(self.cache, f)
        except IOError as e:
            print(f"Warning: Could not save embedding cache: {e}")

    def _hash(self, text: str) -> str:
        """Compute SHA-256 hash of text."""
        return hashlib.sha256(text.encode('utf-8')).hexdigest()

    def get(self, text: str) -> Optional[np.ndarray]:
        """
        Get embedding from cache.

        Args:
            text: Text to look up

        Returns:
            Numpy array embedding if found, None otherwise
        """
        key = self._hash(text)

        if key in self.cache:
            self.hits += 1
            return np.array(self.cache[key], dtype='float32')

        self.misses += 1
        return None

    def put(self, text: str, embedding: np.ndarray):
        """
        Store embedding in cache.

        Args:
            text: Text that was embedded
            embedding: Embedding vector
        """
        key = self._hash(text)
        self.cache[key] = embedding.tolist()

    def save(self):
        """Persist cache to disk."""
        self._save()

    def size(self) -> int:
        """Get number of cached embeddings."""
        return len(self.cache)

    def hit_rate(self) -> float:
        """
        Get cache hit rate.

        Returns:
            Hit rate as percentage (0-100)
        """
        total = self.hits + self.misses
        if total == 0:
            return 0.0
        return (self.hits / total) * 100

    def stats(self) -> Dict:
        """
        Get cache statistics.

        Returns:
            Dict with hits, misses, hit_rate, size
        """
        return {
            'hits': self.hits,
            'misses': self.misses,
            'hit_rate': self.hit_rate(),
            'size': self.size()
        }

    def clear(self):
        """Clear all cached embeddings."""
        self.cache = {}
        self.hits = 0
        self.misses = 0
        self._save()


# Global cache instance
_cache_instance: Optional[EmbeddingCache] = None


def get_cache() -> EmbeddingCache:
    """
    Get or create global cache instance.

    Returns:
        EmbeddingCache instance
    """
    global _cache_instance

    if _cache_instance is None:
        _cache_instance = EmbeddingCache()

    return _cache_instance
