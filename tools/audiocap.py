#!/usr/bin/env python3
"""Capture what the NES emulator hands the speaker, as a WAV, and analyse it.

Usage: audiocap.py out.wav [--tap=X,Y ...]
Taps run first (to get a ROM going), then 4 seconds are recorded.
"""
import glob, math, struct, sys, time, wave
from serial_read import open_quiet


def main():
    out = [a for a in sys.argv[1:] if not a.startswith("--")]
    out = out[0] if out else "capture.wav"
    p = open_quiet(sorted(glob.glob("/dev/cu.usbmodem*"))[0])
    p.timeout = 1.0
    time.sleep(3.5)
    for spec in [a[6:] for a in sys.argv[1:] if a.startswith("--tap=")]:
        p.reset_input_buffer(); p.write(("t%s\n" % spec).encode()); p.flush(); time.sleep(0.8)
    # --pad=HEX,FRAMES presses controller buttons before recording, e.g.
    # --pad=08,20 holds START for 20 frames to get past a silent title screen.
    for spec in [a[6:] for a in sys.argv[1:] if a.startswith("--pad=")]:
        p.write(("j%s\n" % spec).encode()); p.flush(); time.sleep(1.5)
    time.sleep(1.0)
    p.reset_input_buffer()
    p.write(b"a"); p.flush()

    deadline = time.time() + 40
    hdr = b""
    while b"AUDIO " not in hdr:
        hdr += p.read(256)
        if time.time() > deadline: sys.exit("no AUDIO header")
    hdr = hdr[hdr.index(b"AUDIO "):]
    while b"\n" not in hdr: hdr += p.read(64)
    line, rest = hdr.split(b"\n", 1)
    _, n, rate = line.split(); n, rate = int(n), int(rate)
    need = n * 2
    body = rest
    while len(body) < need and time.time() < deadline:
        body += p.read(min(65536, need - len(body)))
    p.close()
    body = body[:need]
    samples = struct.unpack("<%dh" % n, body)

    with wave.open(out, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes(body)

    # --- analysis: peak, RMS, and the noise floor in the quietest 100 ms windows
    peak = max(abs(x) for x in samples)
    rms = math.sqrt(sum(x * x for x in samples) / n)
    win = rate // 10
    winrms = []
    for i in range(0, n - win, win):
        seg = samples[i:i + win]
        winrms.append((math.sqrt(sum(x * x for x in seg) / win), i / rate))
    winrms.sort()
    quiet = winrms[:5]
    loud = winrms[-3:]
    # zero-crossing rate in the quietest window: high = broadband hiss
    qi = int(quiet[0][1] * rate)
    seg = samples[qi:qi + win]
    zc = sum(1 for k in range(1, len(seg)) if (seg[k - 1] < 0) != (seg[k] < 0))
    dc = sum(seg) / len(seg)
    print("wrote %s: %d samples, %.1fs" % (out, n, n / rate))
    print("peak %d (%.2f fs)  rms %.0f" % (peak, peak / 32767, rms))
    print("quietest 100ms windows (rms, t):", ["%.0f@%.1fs" % q for q in quiet])
    print("loudest  100ms windows (rms, t):", ["%.0f@%.1fs" % q for q in loud])
    print("in quietest window: dc=%.0f  zero-crossings=%d (%.0f Hz)  peak=%d" %
          (dc, zc, zc * rate / (2 * win), max(abs(x) for x in seg)))


if __name__ == "__main__":
    main()
