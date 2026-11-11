#!/usr/bin/env python3
"""Check the relative links in the repository's Markdown files.

    python3 .agents/skills/preflight/scripts/check_links.py

Every `[text](target)` outside a code block must point at a file that
exists, and a `#fragment` must match a heading in that file, using GitHub's
heading-to-anchor rule. Links to other sites are not fetched.

Exits 1 and lists the broken links if there are any. Standard library only.
"""

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
LINK = re.compile(r"(?<!\\)\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
FENCE = re.compile(r"^\s*(```|~~~)")
INLINE_CODE = re.compile(r"`[^`]*`")
HEADING = re.compile(r"^#{1,6}\s+(.*?)\s*#*\s*$")
EXTERNAL = re.compile(r"^[a-z][a-z0-9+.-]*:", re.IGNORECASE)


def markdown_files():
    """Tracked files, plus new ones that are not ignored."""
    listed = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "--", "*.md"],
        cwd=ROOT, check=True, capture_output=True, text=True).stdout.splitlines()
    return [ROOT / name for name in listed if (ROOT / name).is_file()]


def prose_lines(path):
    """Lines outside fenced code blocks, with inline code removed."""
    fenced = False
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if FENCE.match(line):
            fenced = not fenced
            continue
        if not fenced:
            yield number, INLINE_CODE.sub("", line), line


def anchors(path, cache={}):
    """The anchors GitHub generates for a file's headings."""
    if path not in cache:
        found, seen = set(), {}
        for _, _, line in prose_lines(path):
            match = HEADING.match(line)
            if not match:
                continue
            text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", match.group(1))
            text = text.replace("`", "").replace("*", "")
            slug = re.sub(r"[^\w\- ]", "", text.lower()).replace(" ", "-")
            count = seen.get(slug, 0)
            seen[slug] = count + 1
            found.add(slug if count == 0 else "{}-{}".format(slug, count))
        cache[path] = found
    return cache[path]


def main():
    checked, broken = 0, []
    for source in markdown_files():
        for number, line, _ in prose_lines(source):
            for target in LINK.findall(line):
                if EXTERNAL.match(target):
                    continue
                checked += 1
                file_part, _, fragment = target.partition("#")
                destination = (source.parent / file_part).resolve() if file_part else source
                where = "{}:{}".format(source.relative_to(ROOT), number)
                if not destination.exists():
                    broken.append("{}: {} does not exist".format(where, target))
                elif fragment and destination.suffix == ".md" \
                        and fragment not in anchors(destination):
                    broken.append("{}: {} has no such heading".format(where, target))

    for problem in broken:
        print(problem)
    print("{} relative links checked, {} broken".format(checked, len(broken)))
    return 1 if broken else 0


if __name__ == "__main__":
    sys.exit(main())
