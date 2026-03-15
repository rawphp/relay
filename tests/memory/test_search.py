"""
Integration tests for semantic memory search.
"""

import pytest
from pathlib import Path
from lib.memory.search import search_memory, load_index, format_results
from lib.memory.build_index import build_index, RELAY_ROOT


@pytest.fixture(scope="module")
def ensure_index():
    """Ensure index is built before tests."""
    build_index(force=False)  # Only rebuild if needed
    yield


def test_load_index(ensure_index):
    """Test that index and metadata load successfully."""
    index, metadata = load_index()

    assert index is not None
    assert index.ntotal > 0
    assert metadata['total_vectors'] == index.ntotal
    assert metadata['dimension'] == 384


def test_search_known_query(ensure_index):
    """Test search for a known concept (semantic search)."""
    results = search_memory("semantic search memory", top_k=5, threshold=0.7)

    assert len(results) > 0  # Should find at least one result

    # Top result should have required fields
    top_result = results[0]
    assert 'text' in top_result
    assert 'score' in top_result
    assert 'file' in top_result
    assert 'lines' in top_result

    # Score should be high
    assert top_result['score'] >= 0.7

    # Text should contain relevant keywords
    text_lower = top_result['text'].lower()
    assert 'semantic' in text_lower or 'search' in text_lower or 'memory' in text_lower


def test_search_document_handling(ensure_index):
    """Test search for document handling feature."""
    results = search_memory("document sending Telegram", top_k=3, threshold=0.8)

    assert len(results) > 0

    # Should find entries about document implementation
    top_result = results[0]
    text_lower = top_result['text'].lower()

    # Should mention documents or telegram
    assert 'document' in text_lower or 'telegram' in text_lower


def test_search_no_results_low_threshold(ensure_index):
    """Test that nonsense query with high threshold returns nothing."""
    results = search_memory("asdfghjkl qwertyuiop", top_k=5, threshold=0.99)

    # Either no results or very few low-confidence results
    assert len(results) <= 1


def test_search_top_k_limit(ensure_index):
    """Test that top_k parameter limits results."""
    results = search_memory("relay daemon", top_k=3, threshold=0.5)

    assert len(results) <= 3


def test_search_threshold_filtering(ensure_index):
    """Test that threshold filters low-score results."""
    high_threshold_results = search_memory("deadline", top_k=10, threshold=0.9)
    low_threshold_results = search_memory("deadline", top_k=10, threshold=0.6)

    # Lower threshold should return more results
    assert len(low_threshold_results) >= len(high_threshold_results)


def test_search_includes_metadata(ensure_index):
    """Test that results include file and line metadata."""
    results = search_memory("memory system", top_k=1, threshold=0.5)

    assert len(results) > 0

    result = results[0]
    assert isinstance(result['file'], str)
    assert result['file'].endswith('.md')
    assert isinstance(result['lines'], tuple)
    assert len(result['lines']) == 2
    assert result['lines'][0] <= result['lines'][1]


def test_search_includes_context(ensure_index):
    """Test that context is included when requested."""
    results = search_memory("testing", top_k=1, threshold=0.5, include_context=True)

    assert len(results) > 0
    assert 'context' in results[0]
    assert isinstance(results[0]['context'], str)
    assert len(results[0]['context']) > 0


def test_format_results():
    """Test result formatting."""
    fake_results = [
        {
            'text': 'Test chunk content',
            'score': 0.85,
            'file': '2026-02-18.md',
            'lines': (10, 15)
        }
    ]

    formatted = format_results(fake_results, verbose=False)

    assert '2026-02-18.md' in formatted
    assert '0.850' in formatted
    assert 'lines 10-15' in formatted


def test_format_empty_results():
    """Test formatting with no results."""
    formatted = format_results([], verbose=False)
    assert 'No results found' in formatted


def test_search_ranking(ensure_index):
    """Test that results are ranked by relevance."""
    results = search_memory("semantic search embeddings", top_k=5, threshold=0.6)

    if len(results) > 1:
        # Scores should be descending
        scores = [r['score'] for r in results]
        assert scores == sorted(scores, reverse=True)
