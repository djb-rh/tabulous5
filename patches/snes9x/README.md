# Patches applied to a fetched SNES core

Applied once, in order, by `tools/optional_snes.py` when it finds the core in
`third_party/snes9x/`. They exist so the core can be fetched unmodified from
wherever you got it: the edits live here rather than in a forked copy.

Each patch should be as small as it can be, and say why it is needed.

* **0001-qsort-comparators-return-int** — the two `qsort` comparators in
  `clip.c` are declared to return `int32_t`. On this toolchain `int32_t` is
  `long`, and `qsort` wants a function returning `int`; the two are the same
  width here but not the same type, and GCC 14 makes that mismatch an error
  rather than a warning.
* **0002-run-the-core-from-flash** — the core marks its 65816 interpreter and
  much of its PPU `IRAM_ATTR`. On the ESP32-P4 the instruction RAM and the data
  RAM are one block of SRAM, so that code is taken directly out of the space
  the rest of the firmware needs for data; with it in place the link fails
  outright, unable to put the small-data section anywhere. A single-purpose
  SNES machine can afford the trade. This one also runs a NES, a Game Boy,
  WiFi and a USB host, so the core runs from flash instead.
