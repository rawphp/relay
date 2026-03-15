#!/usr/bin/env python3
"""
FAISS index builder for semantic memory search.

Chunks memory files, generates embeddings, and builds FAISS index.
"""

import os
import sys
import json
import time
import argparse
from pathlib import Path
from typing import List, Dict, Tuple
import numpy as np
import faiss

# Resolve RELAY_HOME and ensure lib imports work
RELAY_ROOT = Path(os.environ.get("RELAY_HOME", os.path.expanduser("~/relay")))
sys.path.insert(0, str(RELAY_ROOT))

from lib.memory.embeddings import get_model
from lib.memory import frontmatter


# Paths
MEMORY_DIR = RELAY_ROOT / "data" / "memory"
INDEX_DIR = MEMORY_DIR / ".index"
FAISS_INDEX_PATH = INDEX_DIR / "faiss.index"
METADATA_PATH = INDEX_DIR / "metadata.json"
TIMESTAMP_PATH = INDEX_DIR / "last_build.timestamp"


def chunk_memory_file(filepath: Path) -> Tuple[List[Dict], Dict]:
    """
    Split memory file into searchable chunks and extract metadata.

    Strategy:
    - Parse YAML frontmatter for metadata
    - Split on double newlines (paragraphs)
    - Keep metadata headers (##, ###) for context
    - Min chunk size: 20 chars (filter noise)
    - Max chunk size: handled by model (512 tokens)

    Args:
        filepath: Path to memory file

    Returns:
        Tuple of (chunks_list, file_metadata)
        - chunks: List of dicts with keys: text, file, line_start, line_end
        - metadata: Dict with type, importance, tags
    """
    chunks = []
    file_metadata = {
        'type': 'note',
        'importance': 0.5,
        'tags': [],
        'modified_at': filepath.stat().st_mtime
    }

    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
    except Exception as e:
        print(f"Warning: Could not read {filepath}: {e}")
        return chunks, file_metadata

    # Parse frontmatter
    parsed = frontmatter.parse(content)
    file_metadata.update({
        'type': parsed['type'],
        'importance': parsed['importance'],
        'tags': parsed['tags']
    })

    # Use body (content without frontmatter) for chunking
    content = parsed['body']

    # Split into paragraphs
    paragraphs = content.split('\n\n')

    current_line = 1
    current_header = ""

    for para in paragraphs:
        para = para.strip()

        if len(para) < 20:  # Skip very short chunks
            current_line += para.count('\n') + 2  # +2 for the double newline
            continue

        # Check if this is a header line
        if para.startswith('#'):
            current_header = para
            current_line += para.count('\n') + 2
            continue

        # Create chunk with header context
        chunk_text = para
        if current_header:
            chunk_text = f"{current_header}\n\n{para}"

        line_count = para.count('\n') + 1
        line_end = current_line + line_count - 1

        chunks.append({
            'text': chunk_text,
            'file': filepath.name,
            'line_start': current_line,
            'line_end': line_end
        })

        current_line = line_end + 2  # +2 for double newline

    return chunks, file_metadata


def infer_source(filepath: Path) -> str:
    """
    Infer whether a memory file primarily contains facts about the user or the agent.

    Rules:
    - USER.md, PRIORITIES.md  → "user"  (describe the user's context and goals)
    - SOUL.md, IDENTITY.md    → "agent" (the agent's own identity)
    - MEMORY.md, daily files  → "agent" (agent's diary / curated memories)
    - Anything else           → "agent" (default: the agent authors most files)

    Returns:
        "user" or "agent"
    """
    name = filepath.name
    user_files = {"USER.md", "PRIORITIES.md"}
    if name in user_files:
        return "user"
    return "agent"


def get_memory_files() -> List[Path]:
    """
    Get all memory files to index.

    Returns:
        List of Path objects for memory files
    """
    files = []

    # Daily memory files
    if MEMORY_DIR.exists():
        files.extend(sorted(MEMORY_DIR.glob("*.md")))

    # Main MEMORY.md file
    main_memory = RELAY_ROOT / "MEMORY.md"
    if main_memory.exists():
        files.append(main_memory)

    return files


def build_index(force: bool = False) -> Tuple[int, float]:
    """
    Build FAISS index from memory files.

    Args:
        force: Rebuild even if index is fresh

    Returns:
        Tuple of (num_chunks, build_time_seconds)
    """
    start_time = time.time()

    # Create index directory
    INDEX_DIR.mkdir(parents=True, exist_ok=True)

    # Check if rebuild needed
    if not force and TIMESTAMP_PATH.exists():
        with open(TIMESTAMP_PATH, 'r') as f:
            last_build = float(f.read().strip())

        # Check if any memory file is newer
        memory_files = get_memory_files()
        needs_rebuild = any(
            f.stat().st_mtime > last_build
            for f in memory_files
        )

        if not needs_rebuild:
            print("Index is fresh, no rebuild needed (use --force to rebuild anyway)")
            return 0, 0.0

    print("Building FAISS index from memory files...")

    # Load embedding model
    model = get_model()
    print(f"Loaded {model.model_name} (dim={model.embedding_dim})")

    # Chunk all memory files
    all_chunks = []
    file_metadata = {}  # filepath -> metadata
    memory_files = get_memory_files()

    for filepath in memory_files:
        chunks, metadata = chunk_memory_file(filepath)

        # Add file metadata to each chunk
        source = infer_source(filepath)
        for chunk in chunks:
            chunk['file_type'] = metadata['type']
            chunk['file_importance'] = metadata['importance']
            chunk['file_tags'] = metadata['tags']
            chunk['file_modified_at'] = metadata['modified_at']
            chunk['source'] = source  # "user" or "agent"

        all_chunks.extend(chunks)
        file_metadata[filepath.name] = metadata
        print(f"  {filepath.name}: {len(chunks)} chunks (type={metadata['type']}, importance={metadata['importance']:.2f})")

    if not all_chunks:
        print("No chunks found! Check that memory files exist.")
        return 0, 0.0

    print(f"\nTotal chunks: {len(all_chunks)}")

    # Generate embeddings (with caching)
    print("Generating embeddings...")
    texts = [chunk['text'] for chunk in all_chunks]
    embeddings = model.encode(texts, batch_size=32, use_cache=True)
    print(f"Generated {len(embeddings)} embeddings (shape: {embeddings.shape})")

    # Show cache stats
    cache_stats = model.embedding_cache.stats()
    print(f"Cache: {cache_stats['hits']} hits, {cache_stats['misses']} misses ({cache_stats['hit_rate']:.1f}% hit rate)")

    # Save cache
    model.embedding_cache.save()

    # Build FAISS index
    print("Building FAISS index...")
    dimension = model.embedding_dim
    index = faiss.IndexFlatIP(dimension)  # Inner Product (cosine with normalized vectors)
    index.add(embeddings.astype('float32'))
    print(f"Index built with {index.ntotal} vectors")

    # Save index
    faiss.write_index(index, str(FAISS_INDEX_PATH))
    print(f"Saved index to {FAISS_INDEX_PATH}")

    # Save metadata
    metadata = {
        'chunks': all_chunks,
        'model': model.model_name,
        'dimension': dimension,
        'total_vectors': len(all_chunks),
        'build_timestamp': time.time(),
        'files_indexed': [str(f.relative_to(RELAY_ROOT)) for f in memory_files]
    }

    with open(METADATA_PATH, 'w') as f:
        json.dump(metadata, f, indent=2)
    print(f"Saved metadata to {METADATA_PATH}")

    # Save timestamp
    with open(TIMESTAMP_PATH, 'w') as f:
        f.write(str(time.time()))

    build_time = time.time() - start_time
    print(f"\n✅ Index built in {build_time:.2f}s ({len(all_chunks)} chunks)")

    return len(all_chunks), build_time


def main():
    """CLI entry point."""
    parser = argparse.ArgumentParser(
        description="Build FAISS index for semantic memory search"
    )
    parser.add_argument(
        '--force',
        action='store_true',
        help='Force rebuild even if index is fresh'
    )

    args = parser.parse_args()
    build_index(force=args.force)


if __name__ == '__main__':
    main()
