#!/usr/bin/env python3
"""Check relative Markdown links without requiring third-party packages."""

from __future__ import annotations

import re
import sys
import os
from pathlib import Path


LINK_PATTERN = re.compile(r"\[[^\]]+\]\(([^)#\s]+)(?:#[^)]+)?\)")
SKIP_DIRECTORIES = {".git", "build", "Build", "consumer-build", "build-sanitized"}


def _is_generated_directory(name: str) -> bool:
    return name.startswith("build-") or name.startswith("install-")


def iter_markdown_documents(root: Path):
    """Yield project Markdown files while pruning generated/dependency trees."""
    for current, directories, filenames in os.walk(root, topdown=True):
        current_path = Path(current)
        relative = current_path.relative_to(root)
        if relative == Path("external"):
            directories[:] = []
            filenames = [name for name in filenames if name == "README.md"]
        else:
            directories[:] = [
                name
                for name in directories
                if name not in SKIP_DIRECTORIES and not _is_generated_directory(name)
            ]
        for filename in filenames:
            if filename.endswith(".md"):
                yield current_path / filename


def find_failures(root: Path) -> list[str]:
    failures: list[str] = []
    for document in iter_markdown_documents(root):
        text = document.read_text(encoding="utf-8")
        for match in LINK_PATTERN.finditer(text):
            link = match.group(1)
            if link.startswith(("http://", "https://", "mailto:")):
                continue
            target = (document.parent / link).resolve()
            if not target.exists():
                failures.append(f"{document.relative_to(root).as_posix()} -> {link}")
    return failures


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    failures = find_failures(root)

    if failures:
        for failure in failures:
            print(failure, file=sys.stderr)
        return 1
    print("All relative Markdown links resolve.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
