#!/usr/bin/env python3
"""Validate and summarise PhraseCraze word packs.

A pack is a plain-text file, one phrase per line, with optional `# key: value`
header lines and an optional `| difficulty` suffix per phrase:

    # name: Movies & TV
    # color: #E4572E
    # icon: film
    Jurassic Park
    The Great British Bake Off | easy

Usage:
    tools/packs.py                 # validate every pack under data/packs
    tools/packs.py path/to/pack.txt
"""

from __future__ import annotations

import re
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PACK_DIR = REPO_ROOT / "data" / "packs"

VALID_DIFFICULTIES = {"easy", "medium", "hard"}
REQUIRED_HEADERS = ("name", "color", "icon")
COLOR_RE = re.compile(r"^#[0-9A-Fa-f]{6}$")

# The round screen renders one phrase in a large face; past roughly this many
# characters it has to shrink far enough to look wrong.
MAX_PHRASE_LEN = 42


@dataclass
class Pack:
    path: Path
    headers: dict[str, str] = field(default_factory=dict)
    phrases: list[tuple[str, str]] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)


def parse(path: Path) -> Pack:
    pack = Pack(path=path)
    seen: dict[str, int] = {}

    # utf-8-sig strips a BOM, which some editors add and which would otherwise
    # end up glued to the first header key.
    raw = path.read_text(encoding="utf-8-sig")

    for lineno, line in enumerate(raw.splitlines(), start=1):
        line = line.strip()
        if not line:
            continue

        if line.startswith("#"):
            body = line.lstrip("#").strip()
            if ":" in body:
                key, _, value = body.partition(":")
                pack.headers[key.strip().lower()] = value.strip()
            continue

        phrase, _, difficulty = line.partition("|")
        phrase = phrase.strip()
        difficulty = difficulty.strip().lower() or "medium"

        if not phrase:
            pack.errors.append(f"line {lineno}: difficulty with no phrase")
            continue
        if difficulty not in VALID_DIFFICULTIES:
            pack.errors.append(
                f"line {lineno}: unknown difficulty {difficulty!r} "
                f"(expected {'/'.join(sorted(VALID_DIFFICULTIES))})"
            )
            continue
        if len(phrase) > MAX_PHRASE_LEN:
            pack.warnings.append(
                f"line {lineno}: {len(phrase)} chars, will render small — {phrase!r}"
            )

        key = phrase.casefold()
        if key in seen:
            pack.errors.append(
                f"line {lineno}: duplicate of line {seen[key]} — {phrase!r}"
            )
            continue
        seen[key] = lineno

        pack.phrases.append((phrase, difficulty))

    for header in REQUIRED_HEADERS:
        if header not in pack.headers:
            pack.errors.append(f"missing required header: {header}")

    color = pack.headers.get("color")
    if color and not COLOR_RE.match(color):
        pack.errors.append(f"color {color!r} is not #RRGGBB")

    if not pack.phrases:
        pack.errors.append("no phrases")

    return pack


def report(pack: Pack) -> bool:
    name = pack.headers.get("name", pack.path.stem)
    counts = Counter(d for _, d in pack.phrases)
    status = "FAIL" if pack.errors else "ok"

    print(
        f"{status:>4}  {pack.path.name:<22} {len(pack.phrases):>4} phrases  "
        f"({counts['easy']} easy / {counts['medium']} medium / {counts['hard']} hard)"
        f"  {name}"
    )
    for err in pack.errors:
        print(f"        error:   {err}")
    for warn in pack.warnings:
        print(f"        warning: {warn}")

    return not pack.errors


def main(argv: list[str]) -> int:
    if argv:
        paths = [Path(a) for a in argv]
    else:
        paths = sorted(PACK_DIR.glob("*.txt"))

    if not paths:
        print(f"no packs found in {PACK_DIR}", file=sys.stderr)
        return 1

    ok = True
    total = 0
    cross_pack: Counter[str] = Counter()

    for path in paths:
        if not path.is_file():
            print(f"FAIL  {path}: not a file")
            ok = False
            continue
        pack = parse(path)
        ok &= report(pack)
        total += len(pack.phrases)
        cross_pack.update(p.casefold() for p, _ in pack.phrases)

    # A phrase appearing in two packs is legitimate (Popcorn belongs in both
    # Food and Movies), so this is informational, not an error.
    shared = [p for p, n in cross_pack.items() if n > 1]
    print(f"\n{len(paths)} packs, {total} phrases total")
    if shared:
        print(f"{len(shared)} phrase(s) appear in more than one pack: "
              f"{', '.join(sorted(shared)[:8])}"
              f"{' ...' if len(shared) > 8 else ''}")

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
