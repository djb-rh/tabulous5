# Local changes to Peanut-GB

Vendored from https://github.com/deltabeard/Peanut-GB (MIT, Mahyar Koshkouei),
together with `minigb_apu` from its `examples/sdl2` directory (MIT, Alex Baines
and Mahyar Koshkouei). Licence texts are in `LICENSE.peanut_gb` and
`LICENSE.minigb_apu`.

**No source changes.** Both files are byte-for-byte upstream. Everything this
project needs is done through the library's own configuration:

* `ENABLE_SOUND` and `ENABLE_LCD` are defined by `src/gb_ui.cpp` before it
  includes `peanut_gb.h`, along with the `audio_read` / `audio_write` functions
  the header calls by name when sound is on.
* `MINIGB_APU_AUDIO_FORMAT_S16SYS` is a build flag, so `minigb_apu.c` and
  `gb_ui.cpp` agree on the sample format.

`peanut_gb.h` compiles as C++ without complaint, so it is included directly
rather than wrapped in a C translation unit.
