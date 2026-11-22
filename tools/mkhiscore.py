#!/usr/bin/env python3
"""Makes launcher/ports/arcade/hiscore_table.h: where each game on the Pac-Man board keeps its high scores.

    tools/mkhiscore.py ~/Developer/mame/plugins/hiscore/hiscore.dat  folder-of-.arc-files

The regions come from MAME's hiscore.dat (its hiscore plugin's list: address, length, and the values the first and
last byte hold once the game has set its table up). The Arcade app has to know which game a file holds without
trusting the file's name, so each entry also carries the CRC-32 of the ROMs as the app loads them (the .arc's
whole payload): give this the .arc files mkarcade.py makes from a MAME set (`mkarcade.py --all`). The table holds
addresses and checksums only, nothing of the ROMs themselves. Sets with no hiscore.dat entry are left out.
"""
import json, os, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "launcher", "ports", "arcade", "hiscore_table.h")


def read_hiscore_dat(path):
    entries, names, after_regions, dropped = {}, [], False, set()
    for raw in open(path, errors="replace"):
        line = raw.split(";")[0].strip()
        if not line:
            continue
        if line.startswith("@"):
            parts = line.split(",")
            if parts[0] == "@:maincpu" and parts[1] == "program":
                for n in names:
                    entries.setdefault(n, []).append((int(parts[2], 16), int(parts[3], 16), int(parts[4], 16), int(parts[5], 16)))
            else:
                dropped.update(names)                 # anything else (another CPU, @delay): not handled, drop the game
            after_regions = True
        elif line.endswith(":"):
            if after_regions:
                names, after_regions = [], False
            names.append(line[:-1].split(",")[0])
    return {k: v for k, v in entries.items() if k not in dropped}


def cpu_crc(path):
    """CRC-32 of everything after the header: program, a kit's second program, graphics, colour and sound PROMs."""
    d = open(path, "rb").read()
    header = 16 if d[9] == 1 else 32
    payload = int.from_bytes(d[12:16], "little")
    return zlib.crc32(d[header:header + payload]) & 0xFFFFFFFF


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    dat = read_hiscore_dat(sys.argv[1])
    games = json.load(open(os.path.join(HERE, "arcade_games.json")))["games"]
    by_title = {g["title"]: g["set"] for g in games}
    rows, missing_dat, missing_arc = [], [], []
    arcs = {os.path.splitext(f)[0]: os.path.join(sys.argv[2], f) for f in os.listdir(sys.argv[2]) if f.endswith(".arc")}
    for g in sorted(games, key=lambda g: g["set"]):
        s, t = g["set"], g["title"]
        if s not in dat:
            missing_dat.append(s)
            continue
        if t not in arcs:
            missing_arc.append(s)
            continue
        rows.append((s, t, cpu_crc(arcs[t]), dat[s]))
    crcs = [r[2] for r in rows]
    dup = {c for c in crcs if crcs.count(c) > 1}
    with open(OUT, "w") as f:
        f.write("// Made by tools/mkhiscore.py from MAME's hiscore.dat and the program ROMs' CRC-32s: do not edit.\n")
        f.write("// Where each Pac-Man-board game keeps its high scores, and how to know it is set up (MAME's hiscore\n")
        f.write("// plugin's rule: the first byte of each region holds `first` and the last byte `last`).\n#pragma once\n#include <stdint.h>\n\n")
        f.write("struct HiscoreRegion { uint16_t addr, len; uint8_t first, last; };\n")
        f.write("struct HiscoreGame { const char *set, *title; uint32_t cpu_crc; uint8_t count; HiscoreRegion r[6]; };\n\n")
        f.write("static const HiscoreGame kHiscoreGames[] = {\n")
        for s, t, c, regs in rows:
            if len(regs) > 6:
                raise SystemExit("mkhiscore.py: %s has %d regions" % (s, len(regs)))
            rs = ", ".join("{0x%04x, %d, 0x%02x, 0x%02x}" % r for r in regs)
            f.write('    {"%s", "%s", 0x%08x, %d, {%s}},%s\n' % (s, t.replace('"', '\\"'), c, len(regs), rs,
                                                         "   // same program as another set" if c in dup else ""))
        f.write("};\n")
    print("%s: %d games" % (os.path.relpath(OUT), len(rows)))
    if missing_dat:
        print("no hiscore.dat entry: " + ", ".join(missing_dat))
    if missing_arc:
        print("no .arc to take the program's CRC from: " + ", ".join(missing_arc))


if __name__ == "__main__":
    main()
