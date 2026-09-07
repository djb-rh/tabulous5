#!/usr/bin/env python3
"""Inject a tap at panel coordinates, for reaching a screen without hands.

Usage: tap.py X Y [X Y ...]
"""
import glob
import sys
import time

from serial_read import open_quiet


def main():
    coords = [int(a) for a in sys.argv[1:]]
    if not coords or len(coords) % 2:
        sys.exit("usage: tap.py X Y [X Y ...]")
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("no /dev/cu.usbmodem* found")
    p = open_quiet(ports[0])
    for i in range(0, len(coords), 2):
        p.write(b"t%d,%d\n" % (coords[i], coords[i + 1]))
        p.flush()
        print("tap %d,%d" % (coords[i], coords[i + 1]))
        time.sleep(0.4)  # let the repaint finish before the next one
    p.close()


if __name__ == "__main__":
    main()
