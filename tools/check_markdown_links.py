#!/usr/bin/env python3
"""Check relative Markdown links without requiring third-party packages."""

from __future__ import annotations

import re
import sys
from pathlib import Path


LINK_PATTERN = re.compile(r"\[[^\]]+\]\(([^)#\s]+)(?:#[^)]+)?\)")
SKIP_DIRECTORIES = {".git", "build", "external"}


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    failures: list[str] = []
    documents = (
        document
        for document in root.rglob("*.md")
        if not any(part in SKIP_DIRECTORIES for part in document.relative_to(root).parts)
    )
    for document in documents:
        text = document.read_text(encoding="utf-8")
        for match in LINK_PATTERN.finditer(text):
            link = match.group(1)
            if link.startswith(("http://", "https://", "mailto:")):
                continue
            target = (document.parent / link).resolve()
            if not target.exists():
                failures.append(f"{document.relative_to(root)} -> {link}")

    if failures:
        for failure in failures:
            print(failure, file=sys.stderr)
        return 1
    print("All relative Markdown links resolve.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
