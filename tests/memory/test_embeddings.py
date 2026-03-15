"""
Unit tests for embedding model wrapper.
"""

import os
import pytest
import numpy as np
from lib.memory.embeddings import EmbeddingModel, get_model


@pytest.mark.skipif(os.environ.get("RELAY_NO_HF_DOWNLOADS"), reason="HuggingFace access disabled")
def test_embedding_model_initialization():
    """Test that model loads successfully."""
    model = EmbeddingModel()
    assert model.model is not None
    assert model.embedding_dim == 384  # BGE-small-en-v1.5


@pytest.mark.skipif(os.environ.get("RELAY_NO_HF_DOWNLOADS"), reason="HuggingFace access disabled")
def test_encode_single_text():
    """Test encoding a single string."""
    model = get_model()
    embedding = model.encode("Hello world")

    assert isinstance(embedding, np.ndarray)
    assert embedding.shape == (1, 384)
    assert np.all(np.isfinite(embedding))  # No NaN or Inf


@pytest.mark.skipif(os.environ.get("RELAY_NO_HF_DOWNLOADS"), reason="HuggingFace access disabled")
def test_encode_batch():
    """Test encoding multiple texts."""
    model = get_model()
    texts = ["Hello world", "Semantic search", "Vector embeddings"]
    embeddings = model.encode(texts)

    assert embeddings.shape == (3, 384)
    assert np.all(np.isfinite(embeddings))


@pytest.mark.skipif(os.environ.get("RELAY_NO_HF_DOWNLOADS"), reason="HuggingFace access disabled")
def test_embedding_consistency():
    """Test that same text produces same embedding."""
    model = get_model()
    text = "Consistency test"

    emb1 = model.encode(text)
    emb2 = model.encode(text)

    np.testing.assert_array_almost_equal(emb1, emb2, decimal=6)


@pytest.mark.skipif(os.environ.get("RELAY_NO_HF_DOWNLOADS"), reason="HuggingFace access disabled")
def test_similarity_identical_texts():
    """Test that identical texts have similarity ~1.0."""
    model = get_model()
    text = "This is a test"

    similarity = model.similarity(text, text)
    assert similarity > 0.99  # Should be very close to 1.0


@pytest.mark.skipif(os.environ.get("RELAY_NO_HF_DOWNLOADS"), reason="HuggingFace access disabled")
def test_similarity_related_texts():
    """Test that semantically similar texts have high similarity."""
    model = get_model()
    text1 = "The cat sat on the mat"
    text2 = "A feline rested on the rug"

    similarity = model.similarity(text1, text2)
    assert similarity > 0.7  # Should be fairly high


@pytest.mark.skipif(os.environ.get("RELAY_NO_HF_DOWNLOADS"), reason="HuggingFace access disabled")
def test_similarity_unrelated_texts():
    """Test that unrelated texts have low similarity."""
    model = get_model()
    text1 = "Machine learning algorithms"
    text2 = "Cooking pasta recipes"

    similarity = model.similarity(text1, text2)
    assert similarity < 0.5  # Should be low


@pytest.mark.skipif(os.environ.get("RELAY_NO_HF_DOWNLOADS"), reason="HuggingFace access disabled")
def test_normalization():
    """Test that embeddings are normalized (L2 norm = 1)."""
    model = get_model()
    embedding = model.encode("Test text")

    # Compute L2 norm
    norm = np.linalg.norm(embedding[0])
    np.testing.assert_almost_equal(norm, 1.0, decimal=5)


@pytest.mark.skipif(os.environ.get("RELAY_NO_HF_DOWNLOADS"), reason="HuggingFace access disabled")
def test_global_model_singleton():
    """Test that get_model() returns the same instance."""
    model1 = get_model()
    model2 = get_model()

    assert model1 is model2  # Same object instance
