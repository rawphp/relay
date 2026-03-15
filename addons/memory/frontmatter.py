"""
YAML frontmatter parser for memory files.

Supports parsing metadata like type, importance, and tags from markdown files.
"""

import re
from typing import Dict, Any
import yaml


def parse(content: str) -> Dict[str, Any]:
    """
    Parse YAML frontmatter from markdown content.

    Format:
        ---
        type: decision
        importance: 0.9
        tags: [architecture, semantic-search]
        ---

        ## Content starts here

    Args:
        content: Markdown file content (may or may not have frontmatter)

    Returns:
        Dict with keys:
        - type: str (default: 'note')
        - importance: float 0.0-1.0 (default: 0.5)
        - tags: list (default: [])
        - body: str (content without frontmatter)
    """
    # Default values
    metadata = {
        'type': 'note',
        'importance': 0.5,
        'tags': [],
        'body': content
    }

    # Check for frontmatter delimiter
    if not content.startswith('---'):
        return metadata

    # Extract frontmatter block
    pattern = r'^---\s*\n(.*?)\n---\s*\n(.*)$'
    match = re.match(pattern, content, re.DOTALL)

    if not match:
        return metadata

    frontmatter_raw = match.group(1)
    body = match.group(2)

    # Parse YAML
    try:
        frontmatter = yaml.safe_load(frontmatter_raw)
    except yaml.YAMLError:
        # Invalid YAML, return content as-is
        return metadata

    if not isinstance(frontmatter, dict):
        return metadata

    # Extract metadata fields
    metadata['body'] = body

    if 'type' in frontmatter:
        metadata['type'] = str(frontmatter['type'])

    if 'importance' in frontmatter:
        try:
            importance = float(frontmatter['importance'])
            # Clamp to 0.0-1.0 range
            metadata['importance'] = max(0.0, min(1.0, importance))
        except (ValueError, TypeError):
            pass  # Keep default

    if 'tags' in frontmatter:
        if isinstance(frontmatter['tags'], list):
            metadata['tags'] = [str(tag) for tag in frontmatter['tags']]
        elif isinstance(frontmatter['tags'], str):
            # Parse comma-separated tags
            metadata['tags'] = [tag.strip() for tag in frontmatter['tags'].split(',')]

    return metadata


def has_frontmatter(content: str) -> bool:
    """
    Check if content has YAML frontmatter.

    Args:
        content: File content

    Returns:
        True if frontmatter detected
    """
    return content.startswith('---')


def add_frontmatter(content: str, metadata: Dict[str, Any]) -> str:
    """
    Add YAML frontmatter to content.

    Args:
        content: Markdown content (without frontmatter)
        metadata: Dict with type, importance, tags

    Returns:
        Content with frontmatter prepended
    """
    frontmatter_lines = ['---']

    if 'type' in metadata:
        frontmatter_lines.append(f"type: {metadata['type']}")

    if 'importance' in metadata:
        frontmatter_lines.append(f"importance: {metadata['importance']:.2f}")

    if 'tags' in metadata and metadata['tags']:
        tags_str = ', '.join(metadata['tags'])
        frontmatter_lines.append(f"tags: [{tags_str}]")

    frontmatter_lines.append('---')
    frontmatter_lines.append('')

    return '\n'.join(frontmatter_lines) + content
