# Asset credits and licences

## Sound effects — original, generated

Everything in `data/sfx/` is synthesised by [`tools/make_sfx.py`](../tools/make_sfx.py)
from plain arithmetic — sine partials, seeded white noise, a one-pole lowpass
and exponential envelopes. Nothing is sampled from, derived from, or trained on
any third-party recording, so these carry **no attribution requirement and no
licence obligations**. They are part of this project and covered by whatever
licence the project carries.

Regenerate with:

```
tools/make_sfx.py            # writes data/sfx/*.wav
pio run -e tab5 -t uploadfs  # puts them on the device
```

The generator is deterministic (the noise is seeded by effect name), so
re-running it produces byte-identical files and does not churn the repo.

| File | Effect | Length | What it is |
|---|---|---|---|
| `select.wav`  | UI tap                        |   50 ms | soft 2.2 kHz blip with a noise transient |
| `correct.wav` | phrase guessed, card placed   |  340 ms | two rising bell notes, inharmonic partials |
| `skip.wav`    | phrase skipped, move undone   |  190 ms | 880→330 Hz downward swoop |
| `reject.wav`  | illegal move, wrong entry     |  170 ms | blunt 165 Hz thunk |
| `boom.wav`    | Minesweeper mine              |  750 ms | noise through a collapsing lowpass + body tone |
| `buzzer.wav`  | round timer expired           |  950 ms | rasping 300→150 Hz square with vibrato |
| `fanfare.wav` | game won                      | 1150 ms | C–E–G–C with a shimmer on the last note |

All are 22.05 kHz, 16-bit mono, DC-removed, peak-normalised per effect —
`select` sits at 0.30 full scale because it fires on every tap and the others
would drown it out otherwise.

**The accelerating round beep is deliberately not a sample.** Its timing is the
game's tension, so it stays on `M5.Speaker.tone()` where it can be placed
exactly where the round timer says.

## Replacing a sound

`audio::loadSamples()` reads `/sfx/<name>.wav` at boot and falls back to a
synthesised tone for any file that is missing or malformed, so the device is
fully playable on a virgin filesystem. To swap one, drop a WAV in with the same
name and re-run `uploadfs` — no firmware change.

Accepted: uncompressed PCM, 8 or 16 bit, mono or stereo, 4–96 kHz. Anything
else is rejected by name on the boot log rather than played as noise. Stereo is
downmixed and 8-bit is expanded at load time, once, not per play.

If you do want third-party sounds, **CC0 / public domain only** — no
attribution debt. [Kenney.nl](https://kenney.nl/assets?q=audio) game audio packs
are CC0, as is the CC0-filtered subset of OpenGameArt. Note that the bulk of
Freesound is CC-BY, *not* public domain; filter it out rather than quietly
taking on an attribution requirement. Anything added from outside gets a row in
this file naming its source and licence.

## Fonts and graphics

Procedural — drawn with M5GFX primitives, with the fonts that ship with M5GFX.
No bundled font or image files, so nothing to credit here yet.
