"""
Hybrid search combining vector similarity and keyword matching.

Implements 70/30 weighting inspired by Jarvis's production system.
"""

import math
import os
import sys
import time
from pathlib import Path
from datetime import datetime
from typing import List, Dict, Optional
import numpy as np

# Exponential recency decay: score *= exp(-_RECENCY_DECAY_RATE * age_days)
# At 30 days: multiplier = 0.5 (half-life of 30 days)
_RECENCY_DECAY_RATE = math.log(2) / 30

# Resolve RELAY_HOME and ensure lib imports work
RELAY_ROOT = Path(os.environ.get("RELAY_HOME", os.path.expanduser("~/relay")))
if str(RELAY_ROOT) not in sys.path:
    sys.path.insert(0, str(RELAY_ROOT))

from lib.memory.embeddings import get_model
from lib.memory.keyword_search import extract_keywords, keyword_search


def hybrid_search(
    query: str,
    top_k: int = 5,
    vector_weight: float = 0.7,
    keyword_weight: float = 0.3,
    min_score: float = 0.5,
    include_context: bool = True,
    importance_boost: bool = True,
    recency_boost: bool = True,
    deduplicate: bool = True,
    token_budget: int = 500
) -> List[Dict]:
    """
    Hybrid search combining vector similarity and keyword matching.

    Args:
        query: Natural language search query
        top_k: Number of results to return
        vector_weight: Weight for vector similarity (default: 0.7)
        keyword_weight: Weight for keyword matching (default: 0.3)
        min_score: Minimum final score threshold (0-1)
        include_context: Include surrounding lines in results
        importance_boost: Apply importance multiplier from frontmatter
        recency_boost: Apply recency decay based on file age
        deduplicate: Keep only highest-scoring chunk per file
        token_budget: Maximum tokens in results (0 = unlimited)

    Returns:
        List of result dicts with keys:
        - text: chunk content
        - score: final blended score
        - vector_score: raw vector similarity
        - keyword_score: raw keyword coverage
        - file: source file name
        - lines: (line_start, line_end) tuple
        - context: surrounding paragraphs (if include_context=True)
        - type: file type from frontmatter
        - importance: file importance (0-1)
        - days_old: file age in days
    """
    # Load index and metadata (import here to avoid circular dependency)
    from lib.memory.search import load_index
    index, metadata = load_index()

    # Load embedding model
    model = get_model(metadata['model'])

    # Get all chunks
    chunks = metadata['chunks']

    # 1. Vector Search (FAISS)
    query_embedding = model.encode(query).reshape(1, -1).astype('float32')
    vector_scores, vector_indices = index.search(query_embedding, min(len(chunks), 50))  # Get more candidates

    vector_results = {}
    for score, idx in zip(vector_scores[0], vector_indices[0]):
        vector_results[idx] = float(score)

    # 2. Keyword Search
    keywords = extract_keywords(query)
    keyword_results = {}

    if keywords:
        keyword_results = keyword_search(keywords, chunks, case_sensitive=False)
    else:
        # No keywords extracted, use vector-only
        keyword_weight = 0.0
        vector_weight = 1.0

    # 3. Blend Scores
    blended_results = []

    # Combine all chunk indices from both searches
    all_indices = set(vector_results.keys()) | set(keyword_results.keys())

    for idx in all_indices:
        vector_score = vector_results.get(idx, 0.0)
        keyword_score = keyword_results.get(idx, 0.0)

        # 70/30 blending (or adjusted weights)
        blended_score = (vector_weight * vector_score) + (keyword_weight * keyword_score)

        chunk = chunks[idx]

        result = {
            'chunk_id': idx,
            'text': chunk['text'],
            'file': chunk['file'],
            'lines': (chunk['line_start'], chunk['line_end']),
            'score': blended_score,
            'vector_score': vector_score,
            'keyword_score': keyword_score,
            'type': chunk.get('file_type', 'note'),
            'importance': chunk.get('file_importance', 0.5),
            'modified_at': chunk.get('file_modified_at', time.time()),
            'source': chunk.get('source', 'agent'),  # "user" or "agent"
        }

        blended_results.append(result)

    # 4. Apply Importance Boost
    if importance_boost:
        for result in blended_results:
            # Importance: 0.0 → 0.5x, 0.5 → 1.0x, 1.0 → 1.5x
            importance_multiplier = 0.5 + result['importance']
            result['score'] *= importance_multiplier

    # 5. Apply Recency Boost
    if recency_boost:
        now = time.time()
        for result in blended_results:
            # Calculate days old
            days_old = (now - result['modified_at']) / 86400  # seconds to days
            result['days_old'] = int(days_old)

            # Exponential recency decay: half-life = 30 days
            # today → 1.0x, 30 days → 0.5x, 60 days → 0.25x
            recency_multiplier = math.exp(-_RECENCY_DECAY_RATE * days_old)
            result['score'] *= recency_multiplier

    # 6. Filter by minimum score
    filtered_results = [r for r in blended_results if r['score'] >= min_score]

    # 6b. Keyword fallback: if vector search dominates but misses, and keyword
    #     search has hits, retry with keyword-only weights.
    #     Triggered when: no results pass min_score threshold AND keyword_results
    #     is non-empty. This handles queries where vector similarity is low
    #     (niche terms, proper nouns) but exact keyword matching works.
    if not filtered_results and keyword_results:
        keyword_only_threshold = min_score * 0.5  # Lower bar for keyword-only pass
        keyword_only_results = []
        for idx, kw_score in keyword_results.items():
            chunk = chunks[idx]
            result = {
                'chunk_id': idx,
                'text': chunk['text'],
                'file': chunk['file'],
                'lines': (chunk['line_start'], chunk['line_end']),
                'score': kw_score,
                'vector_score': vector_results.get(idx, 0.0),
                'keyword_score': kw_score,
                'type': chunk.get('file_type', 'note'),
                'importance': chunk.get('file_importance', 0.5),
                'modified_at': chunk.get('file_modified_at', time.time())
            }
            if kw_score >= keyword_only_threshold:
                keyword_only_results.append(result)
        if keyword_only_results:
            filtered_results = keyword_only_results

    # 7. Deduplicate by file (keep highest scoring chunk per file)
    if deduplicate:
        by_file = {}
        for result in filtered_results:
            file_name = result['file']
            if file_name not in by_file or result['score'] > by_file[file_name]['score']:
                by_file[file_name] = result

        filtered_results = list(by_file.values())

    # 8. Sort by score descending
    filtered_results.sort(key=lambda x: x['score'], reverse=True)

    # 9. Apply token budget
    if token_budget > 0:
        filtered_results = cap_tokens(filtered_results, token_budget)

    # 10. Limit to top_k
    filtered_results = filtered_results[:top_k]

    # 11. Add context if requested
    if include_context:
        from lib.memory.search import get_context
        from lib.memory.build_index import MEMORY_DIR

        for result in filtered_results:
            # Determine full filepath
            if result['file'] == 'MEMORY.md':
                filepath = RELAY_ROOT / 'MEMORY.md'
            else:
                filepath = MEMORY_DIR / result['file']

            result['context'] = get_context(
                filepath,
                result['lines'][0],
                result['lines'][1],
                context_lines=3
            )

    return filtered_results


def cap_tokens(results: List[Dict], max_tokens: int = 500) -> List[Dict]:
    """
    Limit results to fit within token budget.

    Uses rough estimate: 1 token ≈ 4 characters.

    Args:
        results: List of result dicts
        max_tokens: Maximum tokens allowed

    Returns:
        Results that fit within token budget
    """
    total_chars = 0
    max_chars = max_tokens * 4  # Rough approximation

    capped_results = []

    for result in results:
        content_length = len(result['text'])

        if total_chars + content_length > max_chars:
            break  # Drop remaining results

        total_chars += content_length
        capped_results.append(result)

    return capped_results


def format_hybrid_results(results: List[Dict], verbose: bool = False) -> str:
    """
    Format hybrid search results for display.

    Args:
        results: List of result dicts from hybrid_search()
        verbose: Show full score breakdown

    Returns:
        Formatted string for CLI display
    """
    if not results:
        return "No results found."

    output = []
    output.append(f"\n{'='*80}")
    output.append(f"Found {len(results)} result(s)")
    output.append(f"{'='*80}\n")

    for i, result in enumerate(results, 1):
        # Build score display
        if verbose:
            score_parts = [
                f"final: {result['score']:.3f}",
                f"vec: {result['vector_score']:.3f}",
                f"kw: {result['keyword_score']:.3f}"
            ]
            if 'importance' in result:
                score_parts.append(f"imp: {result['importance']:.2f}")
            if 'days_old' in result:
                score_parts.append(f"age: {result['days_old']}d")

            score_display = f"({', '.join(score_parts)})"
        else:
            score_display = f"Score: {result['score']:.3f}"

        output.append(
            f"[{i}] {result['file']} "
            f"(lines {result['lines'][0]}-{result['lines'][1]}) | "
            f"{score_display}"
        )
        output.append("-" * 80)

        if verbose and 'context' in result:
            output.append(result['context'])
        else:
            # Show just the chunk text
            preview = result['text'][:300]
            if len(result['text']) > 300:
                preview += "..."
            output.append(preview)

        output.append("")

    return '\n'.join(output)
