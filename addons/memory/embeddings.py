"""
Embedding model wrapper for semantic search.

Uses BGE-small-en-v1.5 for local vector embeddings (384-dim).
"""

import os
import sys
from pathlib import Path

# Suppress HuggingFace Hub warnings about authentication (must be set before imports)
os.environ.setdefault('HF_HUB_DISABLE_IMPLICIT_TOKEN', '1')
os.environ.setdefault('TRANSFORMERS_VERBOSITY', 'error')
os.environ.setdefault('HF_HUB_VERBOSITY', 'error')

# Resolve RELAY_HOME and ensure lib imports work
RELAY_ROOT = Path(os.environ.get("RELAY_HOME", os.path.expanduser("~/relay")))
if str(RELAY_ROOT) not in sys.path:
    sys.path.insert(0, str(RELAY_ROOT))

from typing import List, Union
import numpy as np
from sentence_transformers import SentenceTransformer
from lib.memory.cache import get_cache


class EmbeddingModel:
    """Wrapper for BGE-small-en-v1.5 embedding model."""

    def __init__(self, model_name: str = "BAAI/bge-small-en-v1.5", cache_dir: str = None):
        """
        Initialize embedding model.

        Args:
            model_name: HuggingFace model identifier
            cache_dir: Optional directory for caching model files
        """
        if cache_dir is None:
            cache_dir = os.path.expanduser("~/.cache/relay/models")

        os.makedirs(cache_dir, exist_ok=True)

        self.model_name = model_name
        self.cache_dir = cache_dir
        self.model = None
        self.embedding_cache = get_cache()
        self._load_model()

    def _load_model(self):
        """Load the sentence transformer model (suppressing verbose output)."""
        import sys
        import warnings
        import logging

        # Suppress verbose loading output
        old_stderr = sys.stderr
        old_stdout = sys.stdout
        old_log_level = logging.getLogger().level

        try:
            # Redirect stderr/stdout to suppress model loading messages
            sys.stderr = open(os.devnull, 'w')
            sys.stdout = open(os.devnull, 'w')

            # Suppress warnings and logging
            warnings.filterwarnings('ignore')
            logging.getLogger().setLevel(logging.ERROR)

            # Suppress transformers logging specifically
            logging.getLogger('transformers').setLevel(logging.ERROR)

            self.model = SentenceTransformer(
                self.model_name,
                cache_folder=self.cache_dir
            )
        finally:
            # Restore original stderr/stdout/logging
            if sys.stderr != old_stderr:
                sys.stderr.close()
            if sys.stdout != old_stdout:
                sys.stdout.close()
            sys.stderr = old_stderr
            sys.stdout = old_stdout
            logging.getLogger().setLevel(old_log_level)

    def encode(self, texts: Union[str, List[str]], batch_size: int = 32, use_cache: bool = True) -> np.ndarray:
        """
        Generate embeddings for text(s) with caching.

        Args:
            texts: Single string or list of strings to embed
            batch_size: Batch size for encoding (larger = faster but more memory)
            use_cache: Whether to use embedding cache (default: True)

        Returns:
            numpy array of shape (n_texts, 384) for BGE-small-en-v1.5
        """
        is_single = isinstance(texts, str)
        if is_single:
            texts = [texts]

        if not use_cache:
            # Skip cache, directly encode
            return self._encode_batch(texts, batch_size)

        # Check cache for each text
        embeddings = []
        uncached_texts = []
        uncached_indices = []

        for i, text in enumerate(texts):
            cached_emb = self.embedding_cache.get(text)
            if cached_emb is not None:
                embeddings.append((i, cached_emb))
            else:
                uncached_texts.append(text)
                uncached_indices.append(i)

        # Encode uncached texts
        if uncached_texts:
            new_embeddings = self._encode_batch(uncached_texts, batch_size)

            for j, embedding in enumerate(new_embeddings):
                idx = uncached_indices[j]
                embeddings.append((idx, embedding))

                # Cache the new embedding
                self.embedding_cache.put(uncached_texts[j], embedding)

        # Sort by original index and extract embeddings
        embeddings.sort(key=lambda x: x[0])
        result = np.array([emb for _, emb in embeddings])

        # Save cache periodically (every 10 new embeddings)
        if len(uncached_texts) >= 10:
            self.embedding_cache.save()

        return result

    def _encode_batch(self, texts: List[str], batch_size: int) -> np.ndarray:
        """
        Encode texts without caching (internal method).

        Args:
            texts: List of strings to embed
            batch_size: Batch size

        Returns:
            Numpy array of embeddings
        """
        embeddings = self.model.encode(
            texts,
            batch_size=batch_size,
            show_progress_bar=len(texts) > 100,  # Show progress for large batches
            convert_to_numpy=True,
            normalize_embeddings=True  # Normalize for cosine similarity
        )

        return embeddings

    @property
    def embedding_dim(self) -> int:
        """Get embedding dimensionality."""
        return self.model.get_sentence_embedding_dimension()

    def similarity(self, text1: str, text2: str) -> float:
        """
        Compute cosine similarity between two texts.

        Args:
            text1: First text
            text2: Second text

        Returns:
            Similarity score between 0 and 1 (1 = identical)
        """
        emb1, emb2 = self.encode([text1, text2])
        return float(np.dot(emb1, emb2))  # Already normalized, so dot product = cosine


# Global model instance (lazy loaded)
_model_instance = None


def get_model(model_name: str = "BAAI/bge-small-en-v1.5") -> EmbeddingModel:
    """
    Get or create global embedding model instance.

    Args:
        model_name: HuggingFace model identifier

    Returns:
        EmbeddingModel instance
    """
    global _model_instance

    if _model_instance is None or _model_instance.model_name != model_name:
        _model_instance = EmbeddingModel(model_name)

    return _model_instance
