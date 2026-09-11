#!/usr/bin/env python3
"""Packs arcade ROM sets into files the console can read.

An arcade machine's ROMs are a handful of separate chips, distributed as a zip
of one file per chip. The console's game list is one row per file, and teaching
it to open zips would mean carrying an unpacker in the firmware for no other
reason -- so the chips are packed here instead, on the machine that already has
the set, into a single .arc file laid out exactly as the emulator wants it.

    tools/mkarcade.py --all  ~/path/to/MAME*/              /Volumes/CARD/arcade
    tools/mkarcade.py --set pacman ~/path/to/MAME*/        /tmp/out
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


# ---------------------------------------------------------------- Ms. Pac-Man

# Ms. Pac-Man was not a board but a kit: an add-on that plugs into a Pac-Man
# board between the CPU and its ROMs, carrying the new game as an encrypted
# copy of the whole program. It watches the address bus, and swaps its own copy
# in and out as the code runs, so a board that only knows Pac-Man runs a game
# it has never heard of. The decryption and the patching are fixed, so both
# copies of the program are built here; only the swapping is left to the
# firmware.

# Forty eight-byte patches, copied out of the kit's ROMs into Pac-Man's code.
MSPACMAN_PATCHES = [
    (0x0410, 0x8008), (0x08E0, 0x81D8), (0x0A30, 0x8118), (0x0BD0, 0x80D8),
    (0x0C20, 0x8120), (0x0E58, 0x8168), (0x0EA8, 0x8198), (0x1000, 0x8020),
    (0x1008, 0x8010), (0x1288, 0x8098), (0x1348, 0x8048), (0x1688, 0x8088),
    (0x16B0, 0x8188), (0x16D8, 0x80C8), (0x16F8, 0x81C8), (0x19A8, 0x80A8),
    (0x19B8, 0x81A8), (0x2060, 0x8148), (0x2108, 0x8018), (0x21A0, 0x81A0),
    (0x2298, 0x80A0), (0x23E0, 0x80E8), (0x2418, 0x8000), (0x2448, 0x8058),
    (0x2470, 0x8140), (0x2488, 0x8080), (0x24B0, 0x8180), (0x24D8, 0x80C0),
    (0x24F8, 0x81C0), (0x2748, 0x8050), (0x2780, 0x8090), (0x27B8, 0x8190),
    (0x2800, 0x8028), (0x2B20, 0x8100), (0x2B30, 0x8110), (0x2BF0, 0x81D0),
    (0x2CC0, 0x80D0), (0x2CD8, 0x80E0), (0x2CF0, 0x81E0), (0x2D60, 0x8160),
]


def bitswap(value, width, *bits):
    """MAME's bitswap<N>: bits[0] becomes the top bit of the result."""
    out = 0
    for i, b in enumerate(bits):
        out |= ((value >> b) & 1) << (width - 1 - i)
    return out


def _decrypt(byte):
    return bitswap(byte, 8, 0, 4, 5, 7, 6, 3, 2, 1)


def mspacman_banks(rom):
    """The two 32K programs the kit switches between: 16K at 0, 16K at 0x8000.

    `rom` is the romset laid out as MAME loads it: Pac-Man's four ROMs at
    0x0000 and the kit's three at 0x8000, 0x9000 and 0xB000.
    """
    # The board's own bank is Pac-Man, mirrored into the upper addresses.
    plain = bytearray(rom[0x0000:0x4000]) * 2

    # The kit's bank: Pac-Man's first three ROMs, its own decrypted fourth,
    # then its own code up top with more of Pac-Man mirrored after it.
    kit = bytearray(0x8000)
    kit[0x0000:0x3000] = rom[0x0000:0x3000]
    for i in range(0x1000):
        kit[0x3000 + i] = _decrypt(
            rom[0xB000 + bitswap(i, 12, 11, 3, 7, 9, 10, 8, 6, 5, 4, 2, 1, 0)])
    for i in range(0x800):
        kit[0x4000 + i] = _decrypt(
            rom[0x8000 + bitswap(i, 11, 8, 7, 5, 9, 10, 6, 3, 4, 2, 1, 0)])
        kit[0x4800 + i] = _decrypt(
            rom[0x9800 + bitswap(i, 11, 3, 7, 9, 10, 8, 6, 5, 4, 2, 1, 0)])
        kit[0x5000 + i] = _decrypt(
            rom[0x9000 + bitswap(i, 11, 3, 7, 9, 10, 8, 6, 5, 4, 2, 1, 0)])
        kit[0x5800 + i] = rom[0x1800 + i]
    kit[0x6000:0x8000] = rom[0x2000:0x4000]

    # The patches live in the kit's upper half, which sits at 0x8000 on the
    # bus and at 0x4000 in this image.
    for dst, src in MSPACMAN_PATCHES:
        at = dst if dst < 0x4000 else dst - 0x4000
        frm = src - 0x4000
        kit[at:at + 8] = kit[frm:frm + 8]
    return bytes(plain), bytes(kit)


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


def read_sparse(zf, chips, members, size, why):
    """The chips of a region placed at their own offsets, gaps left as 0xFF."""
    by_crc, by_name = members
    out = bytearray(b"\xFF" * size)
    for name, offset, length, crc in chips:
        info = by_crc.get((crc, length)) or by_name.get((name.lower(), length))
        if info is None:
            why.append("%s (%d bytes) is not in the zip" % (name, length))
            return None
        out[offset:offset + length] = zf.read(info)
    return out


def build(game, zip_path):
    """The finished file's bytes, or None and the reason it could not be made."""
    why = []
    if game.get("daughtercard") == "mspacman":
        with zipfile.ZipFile(zip_path) as zf:
            members = index(zf)
            raw = read_sparse(zf, game["cpu"], members, 0x10000, why)
            gfx = read_region(zf, game["gfx"], members, why)
            proms = read_region(zf, game["proms"], members, why)
            sound = read_region(zf, game["sound"], members, why)
        if raw is None or gfx is None or proms is None or sound is None:
            return None, why[0]
        plain, kit = mspacman_banks(raw)
        # Both copies are laid out the way the plain container already is:
        # 16K at 0x0000 then 16K at 0x8000, one after the other.
        payload = plain + kit + bytes(gfx) + bytes(proms) + bytes(sound)
        header = bytearray(HEADER_BYTES)
        header[0:8] = MAGIC
        header[8] = SYSTEM_PACMAN
        header[9] = VERSION
        header[10] = 4  # 16K of program at 0x8000
        header[11] = 0
        header[12:16] = struct.pack("<I", len(payload))
        header[24] = 0 if game.get("upright_monitor", True) else 1
        header[25] = 1  # the Ms. Pac-Man kit
        header[26] = 1 if game.get("eight_way") else 0
        header[27] = 1 if game.get("uses_button") else 0
        return bytes(header) + payload, None

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
    # Zero is the four-way stick nearly all of these had, and is what every
    # file written before this byte meant anything already says.
    header[26] = 1 if game.get("eight_way") else 0
    header[27] = 1 if game.get("uses_button") else 0
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
