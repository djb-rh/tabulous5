# Local changes to the Anemoia core

Upstream: <https://github.com/Shim06/Anemoia-ESP32> (GPL-3.0), by Shim06.
Only `src/core/` is vendored here; the upstream frontend targets TFT_eSPI,
composite video and SD, none of which this device uses.

Changes, kept as small as possible:

* `core/cartridge.cpp` — `SD.open(...)` becomes `LittleFS.open(...)`. This
  device has no SD card slot in use; ROMs live on the internal filesystem.

Nothing else is modified. Upstream is the place to send fixes that are not
about this device.
* `config.h` and `debug.h` are **replacements**, not upstream's. Upstream's
  select between reference boards and define TFT/SD/GPIO pins, and its debug.h
  requires TFT_eSPI. Ours keep only what `core/` actually reads: `VIDEO_STANDARD`
  and the `LOG`/`LOGF` macros. `COMPOSITE_VIDEO` stays undefined — there is no
  composite output here, and defining it changes how the PPU emits pixels.
  Logging is off by default; it sits on per-frame paths.
* `profiler.h` / `profiler.cpp` are upstream's, moved up one directory to sit
  beside `config.h` and `debug.h`; their `#include "../config.h"` becomes
  `#include "config.h"` to match. Kept rather than stubbed because the core has
  14 `PROFILE_SCOPE` points and they answer exactly the question this port
  cares about — where the frame time goes. Compiled out unless
  `ENABLE_PROFILING` is defined.
* `flash_mmap.h` / `flash_mmap.cpp` are upstream's, moved up beside the other
  shared headers. They memory-map a ROM out of a flash partition, which is how
  upstream runs without PSRAM.
* `<SD.h>` becomes `<LittleFS.h>` in `core/cpu6502.h`, `core/mapper.h` and
  `core/cartridge.h`. These only need the Arduino `File` type; this device has
  no card reader wired.
* `core/bus.h` — dropped `#include <TFT_eSPI.h>` and the `TFT_eSPI* ptr_screen`
  member it existed for. Nothing in `bus.cpp` referenced it; this device draws
  through M5GFX.
* `config.h` and `debug.h` are renamed `anemoia_config.h` / `anemoia_debug.h`,
  and every include updated. Their directory has to be on the global include
  path for the core to compile, and `config.h` and `debug.h` are two of the
  most collision-prone filenames there are — with them exposed, any library
  asking for its own `"config.h"` silently got this one instead. That is not a
  theoretical risk: it hung the firmware before `Serial` came up.
* `core/apu2A03.{h,cpp}` — the APU no longer writes to I2S itself. Upstream
  calls `i2s_write()` on the **legacy** driver; M5Unified's speaker uses the
  **new** one, and ESP-IDF calls `abort()` during init if both are linked, so
  the firmware crash-looped before printing a single line. Samples now leave
  through `Apu2A03::setAudioCallback()` and the host plays them. That is the
  right shape here regardless: this device already owns an audio stack.
* `IRAM_ATTR` is now `ANEMOIA_IRAM`, defined empty in `anemoia_config.h`.
  Upstream puts ~99 KB of per-cycle code in internal RAM; define it back to
  `IRAM_ATTR` to try that once the port is running.
* `core/apu2A03.cpp` — `generateSample()` now uses the NES's **non-linear**
  channel mixing (the NESdev formulas, tabulated once) instead of a linear sum
  masked to 8 bits. Upstream's version left only ~187 distinct output levels,
  a noise floor at a fixed absolute level: masked while a sound is loud, and
  audible as hiss on the tail of every effect that fades. The tables give a
  full 16-bit result and the console's real balance between channels. The
  one-pole smoothing is kept but now runs on the full-width value — quantising
  inside the filter's own feedback was adding noise of its own.
