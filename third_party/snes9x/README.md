# SNES core (optional, not shipped)

The SNES emulator is **not** part of this repository and is not distributed
with it. Everything else builds and runs without it; when it is absent the
launcher simply has no SNES entry.

## Why it is not here

The copy of this core in circulation for the ESP32 carries a GNU GPL v2
(or later) notice, but the snes9x code it descends from is under snes9x's own
licence, which forbids commercial use and is not a free software licence.
Those two statements cannot both be right, and this project is not the place
to settle it. So the glue lives here and the core stays with whoever chooses
to fetch it, which is a decision only you can make for your own build.

## How to build with it

Put the core's sources in this directory — either unpacked, or as a `.zip` or
`.tar.gz` that the build will unpack for you (nesting is fine; the build looks
for `cpuops.c` wherever it is). Then build as usual:

```
pio run -e tab5 -t upload
```

The build prints one of:

```
snes9x: found in third_party/snes9x/..., building it in
snes9x: not found, building without SNES (see third_party/snes9x/README.md)
```

A known-working source of a version already adapted for the ESP32-P4 is the
`components/snes9x` directory of
[giltal/RetroESP32-P4](https://github.com/giltal/RetroESP32-P4). Download that
directory and drop its contents here.

Anything in `patches/snes9x/*.patch` is applied once, on first build, to fit
the core to this project. The tree is marked with a `.patched` file afterwards;
delete that (and the sources) to start again.

## What you may do with the result

You can build it and play your own cartridges. You cannot redistribute the
resulting binary: it combines this project's GPL-3.0 code with a core whose
licence terms are, at best, unclear and at worst forbid it. Keep the build to
yourself.

## Everything here except this file is ignored by git

That is deliberate, so a fetched core is never committed by accident.
