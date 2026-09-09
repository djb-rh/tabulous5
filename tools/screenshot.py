#!/usr/bin/env python3
"""Capture the Tab5's panel to a PNG.

Sends 's' over serial; the firmware replies with a header, one raw RGB565
frame, and a trailer. Writes a PNG with nothing but the standard library.

Usage: tools/screenshot.py [out.png] [--tap=X,Y | --sleep=N ...] [--wait=N] [--font] [--raw]

--wait=N counts down N seconds before the capture, which is the window for
navigating to a screen by hand. It deliberately runs AFTER the port is open and
the board has finished rebooting: opening the port resets the board, so a
countdown before that would only be counting down to a reset that throws the
screen away.
"""
import glob
import struct
import sys
import time
import zlib

import serial

from serial_read import open_quiet


def countdown(seconds):
    """Tick down on one line, or a single line when piped to a file."""
    if not sys.stdout.isatty():
        print("waiting %ds before capture..." % seconds)
        time.sleep(seconds)
        return
    for remaining in range(seconds, 0, -1):
        sys.stdout.write("\r  capturing in %2ds — navigate on the device now "
                         % remaining)
        sys.stdout.flush()
        time.sleep(1)
    sys.stdout.write("\r  capturing now...                                  \n")
    sys.stdout.flush()


def read_exact(port, n, deadline):
    buf = bytearray()
    while len(buf) < n:
        if time.time() > deadline:
            raise SystemExit("timed out after %d of %d bytes" % (len(buf), n))
        chunk = port.read(min(65536, n - len(buf)))
        if chunk:
            buf.extend(chunk)
    return bytes(buf)


def write_png(path, width, height, rgb_rows):
    raw = b"".join(b"\x00" + row for row in rgb_rows)

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out = args[0] if args else "screenshot.png"

    wait = 0
    for a in sys.argv[1:]:
        if a.startswith("--wait="):
            try:
                wait = int(a[7:])
            except ValueError:
                sys.exit("--wait wants a whole number of seconds, got %r" % a[7:])
            if wait < 0:
                sys.exit("--wait cannot be negative")
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("no /dev/cu.usbmodem* found")

    p = open_quiet(ports[0])
    p.timeout = 1.0
    p.reset_input_buffer()
    # Settle before driving: opening the port resets the board, and a tap that
    # lands before the first repaint finds no hit targets registered yet.
    time.sleep(3.5)
    # Taps and pauses run in the order given: a screen that takes a while to
    # build (the NES list on a full card) needs --sleep=N after the tap that
    # opens it, or the taps that follow land on whatever was there before.
    for a in sys.argv[1:]:
        if a.startswith("--tap="):
            p.reset_input_buffer()
            p.write(("t%s\n" % a[6:]).encode())
            p.flush()
            time.sleep(0.7)
        elif a.startswith("--pad="):
            # Hold NES buttons, e.g. --pad=08,20 for START. A game that waits
            # on the start button cannot be reached any other way from here:
            # an injected tap goes through the shell's hit targets, and the
            # controls an emulator draws are polled straight off the touch
            # chip instead.
            p.reset_input_buffer()
            p.write(("j%s\n" % a[6:]).encode())
            p.flush()
            time.sleep(0.7)
        elif a.startswith("--sleep="):
            time.sleep(float(a[8:]))

    # Last thing before the shutter, so the countdown means what it says.
    if wait:
        countdown(wait)

    p.reset_input_buffer()
    cmd = b"f" if "--font" in sys.argv else (b"p" if "--panel" in sys.argv else b"s")
    p.write(cmd)
    p.flush()

    deadline = time.time() + 60
    header = b""
    while b"SHOT " not in header:
        if time.time() > deadline:
            sys.exit("no SHOT header; is the firmware current?")
        header += p.read(256)
    for probe in header.split(b"\n"):
        if probe.startswith(b"PROBE"):
            print(probe.decode("ascii", "replace"))
    header = header[header.index(b"SHOT "):]
    while b"\n" not in header:
        header += p.read(64)
    line, rest = header.split(b"\n", 1)
    _, w, h = line.split()
    w, h = int(w), int(h)

    need = w * h * 2
    body = rest + read_exact(p, max(0, need - len(rest)), deadline)
    body = body[:need]
    p.close()

    rows = []
    for y in range(h):
        row = bytearray()
        # Sprites store 565 big-endian (panel byte order); the panel's own
        # framebuffer is host order. Decoding the wrong one shifts the bit
        # boundaries and turns neighbouring shades into unrelated hues.
        fmt = "<" if "--panel" in sys.argv else ">"
        for v in struct.unpack(fmt + "%dH" % w, body[y * w * 2:(y + 1) * w * 2]):
            r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
            # Replicate high bits into low so full scale stays full scale;
            # a plain shift would cap white at 248.
            row += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4),
                          (b << 3) | (b >> 2)))
        rows.append(bytes(row))

    write_png(out, w, h, rows)
    if "--raw" in sys.argv:
        # Only on request: it is 1.8 MB, and its one use is re-decoding a
        # capture locally (tools/raw2png.py) when the colours look wrong.
        with open(out + ".raw", "wb") as f:
            f.write(struct.pack("<II", w, h) + body)
        print("wrote %s (%dx%d) and %s.raw" % (out, w, h, out))
    else:
        print("wrote %s (%dx%d)" % (out, w, h))


if __name__ == "__main__":
    main()
