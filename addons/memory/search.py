#!/usr/bin/env python3
"""
Semantic memory search CLI and API.

Search across memory files using vector embeddings.
"""

import os
import sys
import json
import argparse
from pathlib import Path
from typing import List, Dict, Optional
import numpy as np
import faiss

# Resolve RELAY_HOME and ensure lib imports work
RELAY_ROOT = Path(os.environ.get("RELAY_HOME", os.path.expanduser("~/relay")))
sys.path.insert(0, str(RELAY_ROOT))

from lib.memory.embeddings import get_model
from lib.memory.build_index import build_index, FAISS_INDEX_PATH, METADATA_PATH, MEMORY_DIR
from lib.memory.hybrid_search import hybrid_search, format_hybrid_results


def load_index() -> tuple:
    """
    Load FAISS index and metadata.

    Returns:
        Tuple of (faiss_index, metadata_dict)

    Raises:
        FileNotFoundError: If index doesn't exist
    """
    if not FAISS_INDEX_PATH.exists():
        raise FileNotFoundError(
            f"Index not found at {FAISS_INDEX_PATH}. "
            "Run './lib/memory/build_index.py' first."
        )

    if not METADATA_PATH.exists():
        raise FileNotFoundError(f"Metadata not found at {METADATA_PATH}")

    # Load FAISS index
    index = faiss.read_index(str(FAISS_INDEX_PATH))

    # Load metadata
    with open(METADATA_PATH, 'r') as f:
        metadata = json.load(f)

    return index, metadata


def get_context(filepath: Path, line_start: int, line_end: int, context_lines: int = 3) -> str:
    """
    Get context around a chunk (surrounding lines).

    Args:
        filepath: Path to source file
        line_start: Starting line of chunk
        line_end: Ending line of chunk
        context_lines: Number of lines to include before/after

    Returns:
        Text with surrounding context
    """
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            lines = f.readlines()

        start_idx = max(0, line_start - context_lines - 1)
        end_idx = min(len(lines), line_end + context_lines)

        context = ''.join(lines[start_idx:end_idx])
        return context.strip()
    except Exception as e:
        return f"[Could not load context: {e}]"


def search_memory(
    query: str,
    top_k: int = 5,
    threshold: float = 0.7,
    include_context: bool = True
) -> List[Dict]:
    """
    Semantic search across memory files.

    Args:
        query: Natural language search query
        top_k: Number of results to return
        threshold: Minimum similarity score (0-1)
        include_context: Include surrounding lines in results

    Returns:
        List of result dicts with keys:
        - text: chunk content
        - score: similarity score (0-1)
        - file: source file name
        - lines: (line_start, line_end) tuple
        - context: surrounding paragraphs (if include_context=True)
    """
    # Load index and metadata
    try:
        index, metadata = load_index()
    except FileNotFoundError as e:
        print(f"Error: {e}")
        print("Building index now...")
        build_index(force=True)
        index, metadata = load_index()

    # Load embedding model
    model = get_model(metadata['model'])

    # Encode query
    query_embedding = model.encode(query).reshape(1, -1).astype('float32')

    # Search
    scores, indices = index.search(query_embedding, top_k)

    # Build results
    results = []
    for score, idx in zip(scores[0], indices[0]):
        if score < threshold:
            continue

        chunk = metadata['chunks'][idx]

        result = {
            'text': chunk['text'],
            'score': float(score),
            'file': chunk['file'],
            'lines': (chunk['line_start'], chunk['line_end'])
        }

        # Add context if requested
        if include_context:
            # Determine full filepath
            if chunk['file'] == 'MEMORY.md':
                filepath = RELAY_ROOT / 'MEMORY.md'
            else:
                filepath = MEMORY_DIR / chunk['file']

            result['context'] = get_context(
                filepath,
                chunk['line_start'],
                chunk['line_end'],
                context_lines=3
            )

        results.append(result)

    return results


def format_results(results: List[Dict], verbose: bool = False) -> str:
    """
    Format search results for display.

    Args:
        results: List of result dicts from search_memory()
        verbose: Show full context

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
        output.append(f"[{i}] {result['file']} (lines {result['lines'][0]}-{result['lines'][1]}) | Score: {result['score']:.3f}")
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


def main():
    """CLI entry point."""
    parser = argparse.ArgumentParser(
        description="Semantic search across relay memory files"
    )
    parser.add_argument(
        'query',
        type=str,
        help='Search query (natural language)'
    )
    parser.add_argument(
        '--top-k',
        type=int,
        default=5,
        help='Number of results to return (default: 5)'
    )
    parser.add_argument(
        '--threshold',
        type=float,
        default=0.7,
        help='Minimum similarity score 0-1 (default: 0.7)'
    )
    parser.add_argument(
        '--verbose',
        action='store_true',
        help='Show full context around results'
    )
    parser.add_argument(
        '--rebuild',
        action='store_true',
        help='Force rebuild index before searching'
    )
    parser.add_argument(
        '--hybrid',
        action='store_true',
        default=True,
        help='Use hybrid search (vector + keyword, default: True)'
    )
    parser.add_argument(
        '--vector-only',
        action='store_true',
        help='Use pure vector search (disable keyword matching)'
    )

    args = parser.parse_args()

    # Rebuild if requested
    if args.rebuild:
        print("Rebuilding index...")
        build_index(force=True)
        print()

    # Choose search mode
    if args.vector_only:
        # Pure vector search (Phase 1)
        results = search_memory(
            query=args.query,
            top_k=args.top_k,
            threshold=args.threshold,
            include_context=True
        )
        print(format_results(results, verbose=args.verbose))
    else:
        # Hybrid search (Phase 2) - DEFAULT
        results = hybrid_search(
            query=args.query,
            top_k=args.top_k,
            min_score=args.threshold,
            include_context=True
        )
        print(format_hybrid_results(results, verbose=args.verbose))


if __name__ == '__main__':
    main()
