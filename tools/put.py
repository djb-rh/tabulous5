#!/usr/bin/env python3
"""Copy a file onto the console's SD card over the USB serial link.

Usage: put.py LOCAL_FILE /path/on/card

The card need not come out: the firmware's 'w' command takes the bytes
straight off the serial port and writes them, making folders as needed.
Opening the port resets the board, so this waits for it to boot first.
"""
import glob
import os
import sys
import time

from serial_read import open_quiet


def main():
    if len(sys.argv) != 3 or not sys.argv[2].startswith("/"):
        sys.exit("usage: put.py LOCAL_FILE /path/on/card")
    local, remote = sys.argv[1], sys.argv[2]
    size = os.path.getsize(local)
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("no /dev/cu.usbmodem* found")
    p = open_quiet(ports[0])
    p.timeout = 2.0
    time.sleep(6)
    p.reset_input_buffer()
    p.write(("w%s,%d\n" % (remote, size)).encode())
    p.flush()
    t0 = time.time()
    sent = 0
    p.timeout = 5.0
    with open(local, "rb") as f:
        while True:
            chunk = f.read(4096)
            if not chunk:
                break
            p.write(chunk)
            p.flush()
            # Each chunk is acknowledged once it is on the card; the next is
            # not sent before that, which is what keeps the link from
            # outrunning the card and the console's receive buffer.
            ack = p.read(1)
            if ack != b".":
                sys.exit("\nno acknowledgement after %d bytes (got %r)" % (sent, ack))
            sent += len(chunk)
            if sent % (1 << 20) < 4096:
                sys.stdout.write("\r  %d of %d bytes" % (sent, size))
                sys.stdout.flush()
    print("\r  sent %d bytes in %.1fs" % (sent, time.time() - t0))
    deadline = time.time() + 30
    while time.time() < deadline:
        line = p.readline().decode("utf-8", "replace").strip()
        if line.startswith("put:"):
            print(line)
            break
    p.close()


if __name__ == "__main__":
    main()
