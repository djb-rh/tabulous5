#!/usr/bin/env python3
"""Packs arcade ROM sets into files the console can read.

An arcade machine's ROMs are a handful of separate chips, distributed as a zip
of one file per chip. The console's game list is one row per file, and teaching
it to open zips would mean carrying an unpacker in the firmware for no other
reason -- so the chips are packed here instead, on the machine that already has
the set, into a single .arc file laid out exactly as the emulator wants it.

    tools/mkarcade.py --all  ~/Downloads/MAME*/            /Volumes/CARD/arcade
    tools/mkarcade.py --set pacman ~/Downloads/MAME*/      /tmp/out
    tools/mkarcade.py --list

The console emulates the 1980 Namco Pac-Man board, and a great many games
shipped on that one board. tools/arcade_games.json lists every set MAME says
will run on it unmodified, and --all builds each one whose romset is present.

A few of those sets need their ROMs rearranged before the board can run them:
the data lines are crossed on the Eyes boards, Pac-Man Plus and Jump Shot are
lightly encrypted, and Ponpoko stores its tiles in a different order. All of
that is a fixed transformation of the bytes, so it happens here rather than in
the firmware.

Merged romsets keep clones in subfolders and store any file only once, so a
ROM belonging to one game often sits in another's folder. Files are therefore
matched on their name alone, wherever in the zip they are, and checked against
the size the machine expects.

No ROMs are included with this project, and none are produced without a set
you already have.
"""

import argparse
import glob
import json
import os
import struct
import sys
import zipfile

MAGIC = b"TAB5ARC1"
SYSTEM_PACMAN = 0
VERSION = 2
HEADER_BYTES = 32
CPU_BYTES = 16 * 1024
CPU_HIGH_BANK = 4 * 1024
GFX_BYTES = 8 * 1024
MAX_VECTOR_FIXUPS = 4

TABLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "arcade_games.json")

# Characters a FAT filesystem will not take, and the title is the file name.
BAD_IN_NAMES = '/\\:*?"<>|'


def bitswap8(value, *bits):
    """MAME's bitswap<8>: bits[0] becomes the top bit of the result."""
    out = 0
    for i, b in enumerate(bits):
        out |= ((value >> b) & 1) << (7 - i)
    return out


def cpu_d3d5(data):
    """Eyes and its relatives cross data lines D3 and D5 on the program ROMs."""
    return bytearray(bitswap8(b, 7, 6, 3, 4, 5, 2, 1, 0) for b in data)


def gfx_eyes(data):
    """The same boards cross D4 with D6, and address lines A0 with A2."""
    out = bytearray(data)
    for base in range(0, len(out), 8):
        # A0 and A2 swapped: bytes 1<->4 and 3<->6 of every group of eight.
        picked = [out[base + ((j & 2) | ((j & 1) << 2) | ((j >> 2) & 1))]
                  for j in range(8)]
        for j in range(8):
            out[base + j] = bitswap8(picked[j], 7, 4, 5, 6, 3, 2, 1, 0)
    return out


def gfx_ponpoko(data):
    """Ponpoko stores each tile's halves the other way round."""
    out = bytearray(data)
    half = len(out) // 2
    for i in range(0, half, 0x10):  # characters
        for j in range(8):
            out[i + j], out[i + j + 8] = out[i + j + 8], out[i + j]
    for i in range(half, len(out), 0x20):  # sprites
        for j in range(8):
            a, b, c, d = i + j, i + j + 8, i + j + 0x10, i + j + 0x18
            out[a], out[b], out[c], out[d] = out[d], out[a], out[b], out[c]
    return out


def _epos_decrypt(table, picktable, addr, value):
    method = picktable[(addr & 0x001) | ((addr & 0x004) >> 1) | ((addr & 0x020) >> 3) |
                       ((addr & 0x080) >> 4) | ((addr & 0x200) >> 5)]
    if addr & 0x800:
        method ^= 1
    row = table[method]
    return bitswap8(value, *row[:8]) ^ row[8]


PACPLUS_TABLE = [[7, 6, 5, 4, 3, 2, 1, 0, 0x00], [7, 6, 5, 4, 3, 2, 1, 0, 0x28],
                 [6, 1, 3, 2, 5, 7, 0, 4, 0x96], [6, 1, 5, 2, 3, 7, 0, 4, 0xBE],
                 [0, 3, 7, 6, 4, 2, 1, 5, 0xD5], [0, 3, 4, 6, 7, 2, 1, 5, 0xDD]]
PACPLUS_PICK = [0, 2, 4, 2, 4, 0, 4, 2, 2, 0, 2, 2, 4, 0, 4, 2,
                2, 2, 4, 0, 4, 2, 4, 0, 0, 4, 0, 4, 4, 2, 4, 2]

JUMPSHOT_TABLE = [[7, 6, 5, 4, 3, 2, 1, 0, 0x00], [7, 6, 3, 4, 5, 2, 1, 0, 0x20],
                  [5, 0, 4, 3, 7, 1, 2, 6, 0xA4], [5, 0, 4, 3, 7, 1, 2, 6, 0x8C],
                  [2, 3, 1, 7, 4, 6, 0, 5, 0x6E], [2, 3, 4, 7, 1, 6, 0, 5, 0x4E]]
JUMPSHOT_PICK = [0, 2, 4, 4, 4, 2, 0, 2, 2, 0, 2, 4, 4, 2, 0, 2,
                 5, 3, 5, 1, 5, 3, 5, 3, 1, 5, 1, 5, 5, 3, 5, 3]


def cpu_pacplus(data):
    return bytearray(_epos_decrypt(PACPLUS_TABLE, PACPLUS_PICK, i, b)
                     for i, b in enumerate(data))


def cpu_jumpshot(data):
    return bytearray(_epos_decrypt(JUMPSHOT_TABLE, JUMPSHOT_PICK, i, b)
                     for i, b in enumerate(data))


CPU_TRANSFORMS = {"cpu_d3d5": cpu_d3d5, "cpu_pacplus": cpu_pacplus,
                  "cpu_jumpshot": cpu_jumpshot}
GFX_TRANSFORMS = {"gfx_eyes": gfx_eyes, "gfx_ponpoko": gfx_ponpoko}


def load_table():
    with open(TABLE) as f:
        return json.load(f)["games"]


def index(zf):
    """The zip's members, by checksum and by name, for finding one chip."""
    by_crc, by_name = {}, {}
    for info in zf.infolist():
        by_crc.setdefault((info.CRC, info.file_size), info)
        by_name.setdefault((os.path.basename(info.filename).lower(), info.file_size), info)
    return by_crc, by_name


def read_region(zf, chips, members, why):
    """The chips of one region, in order, joined into one image.

    A chip is found by its checksum rather than its name. A merged romset
    stores each distinct file only once, under whichever game named it first,
    so a clone's folder holds only the files that actually differ and the rest
    are elsewhere in the zip under other names.
    """
    by_crc, by_name = members
    out = bytearray()
    for name, _offset, length, crc in chips:
        info = by_crc.get((crc, length)) or by_name.get((name.lower(), length))
        if info is None:
            why.append("%s (%d bytes) is not in the zip" % (name, length))
            return None
        out += zf.read(info)
    return out


def safe_title(title):
    out = "".join(" " if c in BAD_IN_NAMES else c for c in title)
    return " ".join(out.split()).rstrip(".")


def build(game, zip_path):
    """The finished file's bytes, or None and the reason it could not be made."""
    why = []
    with zipfile.ZipFile(zip_path) as zf:
        members = index(zf)
        cpu = read_region(zf, game["cpu"], members, why)
        high = (read_region(zf, game["cpu_high"], members, why)
                if game["cpu_high"] else bytearray())
        gfx = read_region(zf, game["gfx"], members, why)
        proms = read_region(zf, game["proms"], members, why)
        sound = read_region(zf, game["sound"], members, why)
    if cpu is None or high is None or gfx is None or proms is None or sound is None:
        return None, why[0]

    for step in game["transforms"]:
        if step in CPU_TRANSFORMS:
            cpu = CPU_TRANSFORMS[step](cpu)
            if high:
                high = CPU_TRANSFORMS[step](high)
        elif step in GFX_TRANSFORMS:
            gfx = GFX_TRANSFORMS[step](gfx)
        else:
            return None, "unknown transform %s" % step

    # The board takes whole 4K chips at 0x8000; a short one is padded.
    banks = (len(high) + CPU_HIGH_BANK - 1) // CPU_HIGH_BANK
    high = bytes(high) + b"\xFF" * (banks * CPU_HIGH_BANK - len(high))

    if len(cpu) != CPU_BYTES or len(gfx) != GFX_BYTES:
        return None, "the ROMs are not the sizes the board has"

    fixups = game["vector_fixups"][:MAX_VECTOR_FIXUPS]
    payload = bytes(cpu) + high + bytes(gfx) + bytes(proms) + bytes(sound)
    header = bytearray(HEADER_BYTES)
    header[0:8] = MAGIC
    header[8] = SYSTEM_PACMAN
    header[9] = VERSION
    header[10] = banks
    header[11] = len(fixups)
    # Byte 24 is zero for the usual sideways monitor, which is also what every
    # file written before this byte meant anything says.
    header[24] = 0 if game.get("upright_monitor", True) else 1
    header[12:16] = struct.pack("<I", len(payload))
    for i, (frm, to) in enumerate(fixups):
        header[16 + 2 * i] = frm
        header[17 + 2 * i] = to
    return bytes(header) + payload, None


def find_zip(where, name):
    if os.path.isfile(where):
        return where
    for root in glob.glob(where):
        candidate = os.path.join(root, name + ".zip")
        if os.path.isfile(candidate):
            return candidate
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--all", action="store_true",
                    help="build every game whose romset is present")
    ap.add_argument("--set", metavar="NAME", help="build one MAME set")
    ap.add_argument("--list", action="store_true", help="list the sets we can build")
    ap.add_argument("romsets", nargs="?", help="folder of MAME zips, or one zip")
    ap.add_argument("out_dir", nargs="?", help="where to write the .arc files")
    args = ap.parse_args()

    games = load_table()
    if args.list:
        for g in sorted(games, key=lambda g: g["title"].lower()):
            print("%-11s %-10s %s" % (g["set"], g["zip"], g["title"]))
        print("\n%d sets run on the board we emulate." % len(games))
        return

    if not args.romsets or not args.out_dir:
        ap.error("give a romset folder and an output folder")
    if not args.all and not args.set:
        ap.error("pass --all, or --set NAME")

    wanted = [g for g in games if args.all or g["set"] == args.set]
    if not wanted:
        sys.exit("no set called %r; try --list" % args.set)

    os.makedirs(args.out_dir, exist_ok=True)
    made, missing, failed = 0, 0, []
    for game in sorted(wanted, key=lambda g: g["title"].lower()):
        zip_path = find_zip(args.romsets, game["zip"])
        if zip_path is None:
            missing += 1
            if args.set:
                sys.exit("%s.zip is not in %s" % (game["zip"], args.romsets))
            continue
        data, why = build(game, zip_path)
        if data is None:
            failed.append("%s: %s" % (game["set"], why))
            continue
        out = os.path.join(args.out_dir, safe_title(game["title"]) + ".arc")
        with open(out, "wb") as f:
            f.write(data)
        made += 1
        if args.set:
            print("wrote %s (%d bytes)" % (out, len(data)))

    if args.all:
        print("wrote %d games to %s" % (made, args.out_dir))
        if missing:
            print("%d had no romset here" % missing)
        for line in failed:
            print("  could not build " + line)


if __name__ == "__main__":
    main()
