"""
Keyword-based search for exact-term matching.

Complements vector search by finding exact phrases and technical terms.
"""

import re
from pathlib import Path
from typing import List, Dict, Set
from collections import Counter


# Common English stop words
STOP_WORDS = {
    'the', 'a', 'an', 'and', 'or', 'but', 'in', 'on', 'at', 'to', 'for',
    'of', 'with', 'by', 'from', 'as', 'is', 'was', 'are', 'were', 'been',
    'be', 'have', 'has', 'had', 'do', 'does', 'did', 'will', 'would', 'should',
    'could', 'may', 'might', 'must', 'can', 'this', 'that', 'these', 'those',
    'i', 'you', 'he', 'she', 'it', 'we', 'they', 'what', 'which', 'who',
    'when', 'where', 'why', 'how', 'all', 'each', 'every', 'both', 'few',
    'more', 'most', 'some', 'such', 'no', 'not', 'only', 'own', 'same',
    'so', 'than', 'too', 'very'
}


def extract_keywords(query: str, min_length: int = 2) -> List[str]:
    """
    Extract significant keywords from query.

    Args:
        query: Search query string
        min_length: Minimum keyword length (default: 2)

    Returns:
        List of lowercase keywords (stop words removed)
    """
    # Convert to lowercase
    query = query.lower()

    # Split into words (alphanumeric only)
    words = re.findall(r'\b\w+\b', query)

    # Filter out stop words and short words
    keywords = [
        word for word in words
        if word not in STOP_WORDS and len(word) >= min_length
    ]

    # Remove duplicates while preserving order
    seen = set()
    unique_keywords = []
    for kw in keywords:
        if kw not in seen:
            seen.add(kw)
            unique_keywords.append(kw)

    return unique_keywords


def score_text(text: str, keywords: List[str], case_sensitive: bool = False) -> float:
    """
    Score text based on keyword matches.

    Args:
        text: Text to score
        keywords: List of keywords to search for
        case_sensitive: Whether to do case-sensitive matching

    Returns:
        Score from 0.0 to 1.0 (matches / total_keywords)
    """
    if not keywords:
        return 0.0

    if not case_sensitive:
        text = text.lower()
        keywords = [kw.lower() for kw in keywords]

    # Count matches
    matches = sum(1 for kw in keywords if kw in text)

    return matches / len(keywords)


def keyword_search(
    keywords: List[str],
    chunks: List[Dict],
    case_sensitive: bool = False
) -> Dict[int, float]:
    """
    Search chunks for keyword matches.

    Args:
        keywords: List of keywords to search for
        chunks: List of chunk dicts (must have 'text' key)
        case_sensitive: Whether to do case-sensitive matching

    Returns:
        Dict mapping chunk index to keyword score (0-1)
    """
    if not keywords:
        return {}

    scores = {}

    for i, chunk in enumerate(chunks):
        score = score_text(chunk['text'], keywords, case_sensitive)

        if score > 0:  # Only include chunks with at least one match
            scores[i] = score

    return scores


def get_keyword_counts(text: str, keywords: List[str]) -> Dict[str, int]:
    """
    Count occurrences of each keyword in text.

    Args:
        text: Text to search
        keywords: List of keywords

    Returns:
        Dict mapping keyword to count
    """
    text_lower = text.lower()
    counts = {}

    for kw in keywords:
        kw_lower = kw.lower()
        counts[kw] = text_lower.count(kw_lower)

    return counts


def highlight_keywords(text: str, keywords: List[str], max_length: int = 200) -> str:
    """
    Extract snippet with keywords highlighted.

    Args:
        text: Full text
        keywords: Keywords to highlight
        max_length: Maximum snippet length

    Returns:
        Text snippet with keywords in **bold**
    """
    # Find first keyword position
    text_lower = text.lower()
    first_pos = len(text)

    for kw in keywords:
        pos = text_lower.find(kw.lower())
        if pos != -1 and pos < first_pos:
            first_pos = pos

    if first_pos == len(text):
        # No keywords found, return beginning
        snippet = text[:max_length]
        return snippet + ('...' if len(text) > max_length else '')

    # Extract context around first keyword
    start = max(0, first_pos - 50)
    end = min(len(text), first_pos + max_length)
    snippet = text[start:end]

    # Highlight all keywords (case-insensitive)
    for kw in keywords:
        # Use regex for case-insensitive replacement
        pattern = re.compile(re.escape(kw), re.IGNORECASE)
        snippet = pattern.sub(lambda m: f"**{m.group()}**", snippet)

    # Add ellipsis
    if start > 0:
        snippet = '...' + snippet
    if end < len(text):
        snippet = snippet + '...'

    return snippet
