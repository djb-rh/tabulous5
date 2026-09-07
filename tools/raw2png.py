#!/usr/bin/env python3
"""Re-decode a .raw capture from screenshot.py without touching the device.

The sprite's byte order is the thing most likely to be wrong, so this can
render the same bytes both ways for comparison.

Usage: raw2png.py in.raw out.png [le|be]
"""
import struct
import sys

from screenshot import write_png


def main():
    src, dst = sys.argv[1], sys.argv[2]
    order = sys.argv[3] if len(sys.argv) > 3 else "le"
    fmt = "<" if order == "le" else ">"

    blob = open(src, "rb").read()
    w, h = struct.unpack("<II", blob[:8])
    body = blob[8:]

    rows = []
    for y in range(h):
        row = bytearray()
        for v in struct.unpack("%s%dH" % (fmt, w), body[y * w * 2:(y + 1) * w * 2]):
            r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
            row += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4),
                          (b << 3) | (b >> 2)))
        rows.append(bytes(row))
    write_png(dst, w, h, rows)
    print("wrote %s (%s)" % (dst, order))


if __name__ == "__main__":
    main()
