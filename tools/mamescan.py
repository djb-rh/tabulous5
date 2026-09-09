#!/usr/bin/env python3
"""Reads MAME's Pac-Man driver and writes the table mkarcade.py packs from.

The console emulates one arcade board: the 1980 Namco/Midway Pac-Man board.
Hundreds of games shipped on it -- Namco's own, Midway's, licensed conversions
and a long tail of bootlegs -- and MAME's driver already records, for every
one of them, which chips it used and where each one sat. Rather than transcribe
that by hand, this reads the driver and keeps the sets whose hardware is the
plain board: no protection chip, no CPU encryption beyond a fixed descramble,
nothing our emulator does not have.

    tools/mamescan.py ~/Developer/mame/src/mame/pacman > tools/arcade_games.json

The result is checked in, so building a game needs only the romset. MAME's
source is not required, and no ROM data is copied here -- only names, sizes and
offsets, which are facts about the hardware.
"""

import json
import os
import re
import sys

# Machine configurations that are the plain board, or differ from it only in
# ways the emulator already covers.
#   pacman   the board itself
#   woodpek  the same, with the full 48K memory map decoded
#   crush2   the same, minus a coin counter we do not implement anyway
#   piranha  the same, with two interrupt vectors rewritten by a PAL
#   nmouse   likewise, with three
MACHINES = {"pacman", "woodpek", "crush2", "piranha", "nmouse"}

# A PAL on some boards rewrites the byte the program latches as its interrupt
# vector. MAME models it as a short lookup; so do we, in the file header.
VECTOR_FIXUPS = {
    "piranha": [(0xFA, 0x78)],
    "nmouse": [(0xBF, 0x3C), (0xC6, 0x40)],
}

# Driver init routines that only rearrange ROM data. Anything else -- a
# protection chip, banked memory, a decryption that depends on whether the CPU
# is fetching an opcode -- means the set needs hardware we do not have.
TRANSFORMS = {
    "empty_init": [],
    "init_eyes": ["cpu_d3d5", "gfx_eyes"],
    "init_woodpek": ["gfx_eyes"],
    "init_ponpoko": ["gfx_ponpoko"],
    "init_pacplus": ["cpu_pacplus"],
    "init_jumpshot": ["cpu_jumpshot"],
}


# Sets that pass every test here, and that MAME runs, but that our board does
# not -- found by building them and watching what they draw. Each one needs
# something the emulation does not model.
BROKEN = {
    # Sits in its power-on test for ever. Its Make Trax ancestry shows in the
    # input ports: two lines on IN1 and two DIP switch bits read as protection
    # rather than as switches, and our board answers them the wrong way round.
    # Crush Roller itself, Painter and the Crush Roller bootleg all play, so
    # nothing is lost by leaving this one out.
    "paintrlr",
}


def split_args(s):
    out, cur, depth, in_string = [], "", 0, False
    for ch in s:
        if in_string:
            cur += ch
            if ch == '"':
                in_string = False
            continue
        if ch == '"':
            in_string = True
            cur += ch
            continue
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
            continue
        cur += ch
    out.append(cur.strip())
    return out


def read_driver(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)

    sets = {}
    for m in re.finditer(r"ROM_START\(\s*(\w+)\s*\)(.*?)ROM_END", text, re.S):
        name, body = m.group(1), m.group(2)
        regions, region, plain = {}, None, True
        for line in body.splitlines():
            line = line.strip()
            r = re.match(r"ROM_REGION\(\s*(?:0x[0-9a-fA-F]+|\d+)\s*,\s*\"([^\"]+)\"", line)
            if r:
                region = r.group(1)
                regions.setdefault(region, [])
                continue
            l = re.match(r"ROM_LOAD\(\s*\"([^\"]+)\"\s*,\s*(0x[0-9a-fA-F]+)\s*,"
                         r"\s*(0x[0-9a-fA-F]+)(?:.*?CRC\(([0-9a-fA-F]{8})\))?", line)
            if l and region is not None:
                # The CRC is how a chip is found: a merged romset stores each
                # distinct file once, under whichever game named it first, so
                # the name MAME uses here is often not the name in the zip.
                regions[region].append([l.group(1), int(l.group(2), 16),
                                        int(l.group(3), 16),
                                        int(l.group(4), 16) if l.group(4) else 0])
                continue
            if re.match(r"ROM_(CONTINUE|RELOAD|FILL|COPY|LOAD16|LOAD32|IGNORE)", line):
                plain = False
        sets[name] = {"regions": regions, "plain": plain}

    games = {}
    for m in re.finditer(r"^\s*GAMEL?\s*\((.*)$", text, re.M):
        a = split_args(m.group(1).rstrip().rstrip(")"))
        if len(a) < 11:
            continue
        games[a[1]] = {"parent": a[2], "machine": a[3], "init": a[6],
                       "title": a[9].strip('"'), "flags": a[10]}
    return sets, games


def contiguous(loads, start, want):
    """The chips in this region cover want bytes from start, back to back."""
    at = start
    for _name, off, length, _crc in sorted(loads, key=lambda x: x[1]):
        if off != at:
            return False
        at += length
    return at == start + want


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser(
        "~/Developer/mame/src/mame/pacman")
    sets, games = {}, {}
    for name in sorted(os.listdir(src)):
        if not name.endswith(".cpp"):
            continue
        s, g = read_driver(os.path.join(src, name))
        sets.update(s)
        games.update(g)

    out, skipped = [], {}

    def skip(name, why):
        skipped.setdefault(why, []).append(name)

    for name in sorted(sets):
        info, game = sets[name], games.get(name)
        if game is None:
            skip(name, "no GAME line")
            continue
        if not info["plain"]:
            skip(name, "ROM loaded in a way we do not parse")
            continue
        if "NOT_WORKING" in game["flags"]:
            skip(name, "MAME says it does not work")
            continue
        if name in BROKEN:
            skip(name, "runs in MAME but not on our board")
            continue
        if game["machine"] not in MACHINES:
            skip(name, "board is %s, not ours" % game["machine"])
            continue
        if game["init"] not in TRANSFORMS:
            skip(name, "needs %s" % game["init"])
            continue

        r = info["regions"]
        # PAL dumps are recorded for preservation; the board does not read them.
        extra = set(r) - {"maincpu", "gfx1", "proms", "namco", "plds", "pals"}
        if extra:
            skip(name, "has a %s ROM we cannot use" % sorted(extra)[0])
            continue

        cpu = sorted(r.get("maincpu", []), key=lambda x: x[1])
        low = [c for c in cpu if c[1] < 0x4000]
        high = [c for c in cpu if c[1] >= 0x8000]
        if len(low) + len(high) != len(cpu):
            skip(name, "program ROM sits somewhere unexpected")
            continue
        if not contiguous(low, 0x0000, 0x4000):
            skip(name, "program ROM is not a full 16K")
            continue
        # Boards with a 48K map fill 0x8000 upwards; some stop at 8K.
        if high and not any(contiguous(high, 0x8000, n)
                            for n in (0x1000, 0x2000, 0x3000, 0x4000)):
            skip(name, "upper program ROM is not a whole number of chips")
            continue
        if not contiguous(sorted(r.get("gfx1", []), key=lambda x: x[1]), 0, 0x2000):
            skip(name, "tile ROM is not 8K")
            continue
        proms = sorted(r.get("proms", []), key=lambda x: x[1])
        if [(o, l) for _n, o, l, _c in proms] != [(0x00, 0x20), (0x20, 0x100)]:
            skip(name, "colour PROMs are not the pair we expect")
            continue
        sound = sorted(r.get("namco", []), key=lambda x: x[1])
        if [(o, l) for _n, o, l, _c in sound] != [(0x000, 0x100), (0x100, 0x100)]:
            skip(name, "sound PROMs are not the pair we expect")
            continue

        out.append({
            "set": name,
            "title": game["title"],
            # Merged romsets store each file once, in the parent's zip.
            "zip": game["parent"] if game["parent"] != "0" else name,
            "transforms": TRANSFORMS[game["init"]],
            "vector_fixups": VECTOR_FIXUPS.get(game["machine"], []),
            "cpu": [[n, o, l, c] for n, o, l, c in low],
            "cpu_high": [[n, o, l, c] for n, o, l, c in high],
            "gfx": [[n, o, l, c] for n, o, l, c in sorted(r["gfx1"], key=lambda x: x[1])],
            "proms": [[n, o, l, c] for n, o, l, c in proms],
            "sound": [[n, o, l, c] for n, o, l, c in sound],
        })

    json.dump({"games": out}, sys.stdout, indent=1, sort_keys=True)
    sys.stdout.write("\n")
    sys.stderr.write("kept %d of %d sets\n" % (len(out), len(sets)))
    for why in sorted(skipped, key=lambda w: -len(skipped[w])):
        sys.stderr.write("  %3d  %s\n" % (len(skipped[why]), why))


if __name__ == "__main__":
    main()
