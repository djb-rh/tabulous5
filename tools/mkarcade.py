#!/usr/bin/env python3
"""Packs an arcade ROM set into one file the console can read.

An arcade machine's ROMs are a handful of separate chips, distributed as a zip
of one file per chip. The console's game list is one row per file, and teaching
it to open zips would mean carrying an unpacker in the firmware for no other
reason -- so the chips are packed here instead, on the machine that already has
the set, into a single .arc file laid out exactly as the emulator wants it.

    tools/mkarcade.py pacman   ~/Downloads/MAME*/puckman.zip   /tmp/out
    tools/mkarcade.py mspacman ~/Downloads/MAME*/mspacman.zip  /tmp/out

Merged romsets keep clones in subfolders and store any file only once, so a
ROM belonging to one game often sits in another's folder. Files are therefore
matched on their name alone, wherever in the zip they are, and checked against
the size the machine expects.

No ROMs are included with this project, and none are produced without a set
you already have.
"""

import os
import struct
import sys
import zipfile

MAGIC = b"TAB5ARC1"
SYSTEM_PACMAN = 0

# Each region: the names it may go by, and exactly how big it has to be.
# Alternatives exist because the same chip is labelled differently between
# Puck Man, Pac-Man and the Ms. Pac-Man bootlegs.
GAMES = {
    "pacman": {
        "title": "Pac-Man",
        "regions": [
            (["pacman.6e"], 4096), (["pacman.6f"], 4096),
            (["pacman.6h"], 4096), (["pacman.6j"], 4096),
            (["pacman.5e"], 4096), (["pacman.5f"], 4096),
            (["pm1-1.7f", "82s123.7f"], 32),
            (["pm1-4.4a", "82s126.4a"], 256),
            (["pm1-3.1m", "82s126.1m"], 256),
            (["pm1-2.3m", "82s126.3m"], 256),
        ],
    },
    # The bootleg that runs on unmodified Pac-Man hardware: the daughterboard
    # the original needs is folded into these six ROMs.
    "mspacman": {
        "title": "Ms. Pac-Man",
        "regions": [
            (["boot1"], 4096), (["boot2"], 4096),
            (["boot3"], 4096), (["boot4"], 4096),
            (["boot5"], 4096), (["boot6"], 4096),
            (["82s123.7f", "pm1-1.7f"], 32),
            (["82s126.4a", "pm1-4.4a"], 256),
            (["82s126.1m", "pm1-3.1m"], 256),
            (["82s126.3m", "pm1-2.3m"], 256),
        ],
    },
}


def find(zf, names, size):
    """The first member whose base name and size match, wherever it sits."""
    wanted = [n.lower() for n in names]
    for info in zf.infolist():
        if os.path.basename(info.filename).lower() in wanted and info.file_size == size:
            return zf.read(info)
    return None


def main():
    if len(sys.argv) != 4:
        sys.exit("usage: mkarcade.py <%s> <romset.zip> <output dir>"
                 % "|".join(sorted(GAMES)))
    game, zip_path, out_dir = sys.argv[1:4]
    if game not in GAMES:
        sys.exit("unknown game %r; known: %s" % (game, ", ".join(sorted(GAMES))))
    spec = GAMES[game]

    with zipfile.ZipFile(zip_path) as zf:
        chunks = []
        for names, size in spec["regions"]:
            data = find(zf, names, size)
            if data is None:
                sys.exit("%s: could not find %s (%d bytes) in %s"
                         % (game, " or ".join(names), size, os.path.basename(zip_path)))
            chunks.append(data)

    payload = b"".join(chunks)
    header = MAGIC + struct.pack("<BBHI", SYSTEM_PACMAN, 1, 0, len(payload))
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, spec["title"] + ".arc")
    with open(out, "wb") as f:
        f.write(header + payload)
    print("wrote %s (%d bytes)" % (out, len(header) + len(payload)))


if __name__ == "__main__":
    main()
