#!/usr/bin/env python3
"""Inject a drag at panel coordinates: a finger that lands at one point,
travels to another over some milliseconds, and lifts.

Usage: drag.py X0 Y0 X1 Y1 [MS]

The lists scroll under a real finger and reorder from a held handle; a tap
cannot reach either, so this is how they are driven without hands.
"""
import glob
import sys
import time

from serial_read import open_quiet


def main():
    a = [int(v) for v in sys.argv[1:]]
    if len(a) not in (4, 5):
        sys.exit("usage: drag.py X0 Y0 X1 Y1 [MS]")
    ms = a[4] if len(a) == 5 else 300
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("no /dev/cu.usbmodem* found")
    p = open_quiet(ports[0])
    p.write(b"g%d,%d,%d,%d,%d\n" % (a[0], a[1], a[2], a[3], ms))
    p.flush()
    print("drag %d,%d -> %d,%d over %d ms" % (a[0], a[1], a[2], a[3], ms))
    time.sleep(ms / 1000.0 + 0.5)
    p.close()


if __name__ == "__main__":
    main()
