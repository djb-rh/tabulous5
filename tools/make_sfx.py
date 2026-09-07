#!/usr/bin/env python3
"""Generate Tabulous5's sound effects as 22.05 kHz 16-bit mono WAVs.

These are synthesised here rather than sampled from a library so the project
owns them outright — no attribution file to keep in sync, no licence to audit,
and a one-line change regenerates the whole set when a sound turns out to be
annoying at party volume. Anything in data/sfx/ overrides the built-in beeps at
runtime, so a CC0 sample can still replace any of these by dropping a file in
with the same name.

Deterministic: the noise is seeded, so re-running produces byte-identical files
and the repo does not churn.

Usage: tools/make_sfx.py [outdir]        (default: data/sfx)
"""
import math
import os
import random
import struct
import sys
import wave

RATE = 22050


# ----------------------------------------------------------------- primitives

def silence(dur):
    return [0.0] * int(dur * RATE)


def add(buf, start, samples, gain=1.0):
    """Mix `samples` into `buf` at `start` seconds, growing buf as needed."""
    at = int(start * RATE)
    need = at + len(samples)
    if need > len(buf):
        buf.extend([0.0] * (need - len(buf)))
    for i, s in enumerate(samples):
        buf[at + i] += s * gain


def env(n, attack, tau, sustain=0.0):
    """Attack ramp then exponential decay. `sustain` holds the peak first."""
    a = max(1, int(attack * RATE))
    hold = int(sustain * RATE)
    out = []
    for i in range(n):
        if i < a:
            e = i / a
        elif i < a + hold:
            e = 1.0
        else:
            e = math.exp(-(i - a - hold) / (tau * RATE))
        out.append(e)
    return out


def sweep(n, f0, f1, shape="exp"):
    """Per-sample phase for a frequency glide, so there are no phase clicks."""
    out = []
    phase = 0.0
    for i in range(n):
        t = i / max(1, n - 1)
        f = f0 * (f1 / f0) ** t if shape == "exp" else f0 + (f1 - f0) * t
        phase += 2 * math.pi * f / RATE
        out.append(phase)
    return out


def tone(dur, freq, partials=((1.0, 1.0),), attack=0.003, tau=0.12, sustain=0.0):
    n = int(dur * RATE)
    e = env(n, attack, tau, sustain)
    out = []
    for i in range(n):
        v = 0.0
        for mult, amp in partials:
            v += amp * math.sin(2 * math.pi * freq * mult * i / RATE)
        out.append(v * e[i])
    return out


def glide(dur, f0, f1, attack=0.003, tau=0.08, harmonic=0.0):
    n = int(dur * RATE)
    ph = sweep(n, f0, f1)
    e = env(n, attack, tau)
    return [(math.sin(p) + harmonic * math.sin(3 * p)) * e[i]
            for i, p in enumerate(ph)]


def noise(dur, rng, lp0=None, lp1=None, attack=0.002, tau=0.15, sustain=0.0):
    """White noise, optionally through a one-pole lowpass whose cutoff glides
    from lp0 to lp1 — that fall is what turns a hiss into an explosion."""
    n = int(dur * RATE)
    e = env(n, attack, tau, sustain)
    out = []
    y = 0.0
    for i in range(n):
        x = rng.uniform(-1.0, 1.0)
        if lp0:
            t = i / max(1, n - 1)
            cutoff = lp0 * ((lp1 or lp0) / lp0) ** t
            a = 1.0 - math.exp(-2 * math.pi * cutoff / RATE)
            y += a * (x - y)
            x = y
        out.append(x * e[i])
    return out


def square_glide(dur, f0, f1, duty=0.5, vibrato_hz=0.0, vibrato=0.0,
                 attack=0.006, tau=0.12, sustain=0.0):
    n = int(dur * RATE)
    e = env(n, attack, tau, sustain)
    out = []
    phase = 0.0
    for i in range(n):
        t = i / max(1, n - 1)
        f = f0 * (f1 / f0) ** t
        if vibrato_hz:
            f *= 1.0 + vibrato * math.sin(2 * math.pi * vibrato_hz * i / RATE)
        phase += f / RATE
        frac = phase - math.floor(phase)
        # Slightly soft edges: a hard square through a small speaker is mostly
        # unpleasant high harmonics that the Tab5 cannot reproduce anyway.
        v = 1.0 if frac < duty else -1.0
        v = 0.8 * v + 0.2 * math.sin(2 * math.pi * phase)
        out.append(v * e[i])
    return out


def write(path, buf, peak=0.7):
    # Remove DC before scaling. An asymmetric duty cycle (the buzzer) leaves a
    # few percent of offset, which costs speaker excursion for no sound and
    # thumps at the start and end of playback.
    if buf:
        mean = sum(buf) / len(buf)
        buf = [s - mean for s in buf]
    hi = max((abs(s) for s in buf), default=0.0)
    scale = (peak / hi) if hi > 0 else 0.0
    frames = b"".join(
        struct.pack("<h", max(-32768, min(32767, int(s * scale * 32767))))
        for s in buf)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(frames)
    return len(frames) // 2


# --------------------------------------------------------------- the effects

def make_select(rng):
    """Fires on every tap, so it has to be quiet and very short — this one is
    heard more than all the others combined."""
    buf = silence(0.05)
    add(buf, 0, tone(0.045, 2200, ((1.0, 1.0), (1.5, 0.25)), 0.0015, 0.009))
    add(buf, 0, noise(0.004, rng, 3000, 6000, 0.0005, 0.002), 0.35)
    return buf, 0.30


def make_correct(rng):
    """Two rising bell notes. Slightly inharmonic partials (2.01, 3.02) so it
    rings like a bell rather than sitting there like an organ."""
    buf = silence(0.34)
    bell = ((1.0, 1.0), (2.01, 0.28), (3.02, 0.10))
    add(buf, 0.00, tone(0.20, 1319, bell, 0.003, 0.11))
    add(buf, 0.09, tone(0.25, 1976, bell, 0.003, 0.13))
    return buf, 0.60


def make_skip(rng):
    """A short downward swoop: 'moving on', not 'wrong'."""
    buf = silence(0.19)
    add(buf, 0, glide(0.16, 880, 330, 0.004, 0.055, harmonic=0.22))
    return buf, 0.48


def make_reject(rng):
    """Low, blunt and over quickly — an illegal move should feel like a closed
    door, not a punishment."""
    buf = silence(0.17)
    add(buf, 0, tone(0.15, 165, ((1.0, 1.0), (2.0, 0.40), (2.98, 0.12)),
                     0.002, 0.045))
    add(buf, 0, noise(0.05, rng, 900, 300, 0.001, 0.02), 0.25)
    return buf, 0.55


def make_boom(rng):
    """Minesweeper's mine. Noise with a collapsing lowpass over a falling body
    tone; the body stops at 55 Hz because a 1 W speaker cannot do sub-bass and
    trying just wastes headroom."""
    buf = silence(0.75)
    add(buf, 0, noise(0.70, rng, 6000, 260, 0.004, 0.20), 1.0)
    add(buf, 0, glide(0.35, 150, 55, 0.003, 0.13), 0.85)
    return buf, 0.85


def make_buzzer(rng):
    """Time's up. Tonal and harsh, deliberately unlike the explosion, and well
    below the beep ladder so it can never be mistaken for one more tick."""
    buf = silence(0.95)
    body = square_glide(0.90, 300, 150, duty=0.42, vibrato_hz=7.0,
                        vibrato=0.045, attack=0.006, tau=0.10, sustain=0.62)
    # A slow tremolo is what makes it rasp rather than drone.
    for i in range(len(body)):
        body[i] *= 0.82 + 0.18 * math.sin(2 * math.pi * 22.0 * i / RATE)
    add(buf, 0, body)
    add(buf, 0, noise(0.90, rng, 1200, 600, 0.006, 0.10, sustain=0.62), 0.10)
    return buf, 0.62


def make_fanfare(rng):
    """C-E-G-C. Short enough that winning does not stop play for a second."""
    buf = silence(1.15)
    horn = ((1.0, 1.0), (2.0, 0.35), (3.0, 0.15), (4.0, 0.06))
    for start, freq in ((0.00, 523), (0.13, 659), (0.26, 784)):
        add(buf, start, tone(0.30, freq, horn, 0.006, 0.10))
    add(buf, 0.40, tone(0.70, 1047, horn, 0.006, 0.26))
    add(buf, 0.40, tone(0.65, 2093, ((1.0, 1.0), (2.01, 0.2)), 0.010, 0.20), 0.14)
    return buf, 0.66


EFFECTS = {
    "select": make_select,
    "correct": make_correct,
    "skip": make_skip,
    "reject": make_reject,
    "boom": make_boom,
    "buzzer": make_buzzer,
    "fanfare": make_fanfare,
}


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "data/sfx"
    os.makedirs(outdir, exist_ok=True)
    total = 0
    for name, fn in sorted(EFFECTS.items()):
        rng = random.Random(0x5F3759DF ^ hash(name) & 0xffffffff)
        rng.seed(name)  # name-seeded: deterministic across runs and machines
        buf, peak = fn(rng)
        path = os.path.join(outdir, name + ".wav")
        n = write(path, buf, peak)
        size = os.path.getsize(path)
        total += size
        print("%-10s %6d samples  %5.0f ms  %6.1f KB" %
              (name, n, 1000.0 * n / RATE, size / 1024.0))
    print("%-10s %34.1f KB total" % ("", total / 1024.0))


if __name__ == "__main__":
    main()
