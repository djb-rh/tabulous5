# Tabulous5 on P4 Launcher

The console's games as apps for [P4 Launcher](https://github.com/giltal/ESP32_P4_Launcher) (MIT), which loads
programs from the SD card. One firmware, every game, no more choosing between three images: the launcher's own
NES, Game Boy, SNES, Doom and Quake take the place of ours, and what is ours runs beside them from the same menu.

The classic single-image firmware is still the rest of this repository (`src/`, releases up to v1.5.0) and still
builds from this branch. Nothing here changes it, apart from two small fixes to shared files noted below.

## What runs (measured on two Tab5s, one with the rev A gamepad, 2026-11-20)

| App | From | State |
|---|---|---|
| Arcade (Pac-Man board) | `ports/arcade` + `src/arcrom`, `src/fourway`, `third_party/chips`, `third_party/z80` | 60 fps, no audio underruns; cabinet switches, cocktail table with a second stick, face buttons as a stick |
| PhraseCraze, FiveHead, Joshua | `ports/tabgame` + the games' own files in `src/` | run, with the console's sounds, settings and word packs; tilt and the picture turning over need patch 4 |
| Minesweeper, Sudoku, Solitaire | the same | run, score tables kept |
| NES, Game Boy, SNES, Doom, Quake | the launcher's own apps | NES 60 fps, Game Boy 55, SNES 60 emulated / 30 shown, Doom 35, Quake 20 |

The games' source files are compiled unchanged. `ports/m5compat` stands in for what they use of the console:
`M5.Display` (a LovyanGFX sprite whose pixels are the launcher's surface), `M5.Touch`, `M5.Speaker` (a small mixer
on one launcher audio stream), `Preferences` (a file per namespace under `/sd/tabulous`), `LittleFS` (the folder
`/sd/tabulous`). `ports/tabgame/main.cpp` is the shell: the About page, the leave confirmation, the main loop.

**Tilt.** FiveHead is scored by tilting the console and PhraseCraze turns its picture over to face whoever holds
it, both from the accelerometer, which the stock launcher does not give apps. `patches/4-tab5-accelerometer.patch`
adds it for the Tab5 (below); the games use the console's own `src/orientation.cpp` and tilt code unchanged. On a
launcher without the patch FiveHead is played with its GOT IT and PASS buttons and PhraseCraze turns over only
by its FLIP button.

Not carried over:

- The on-screen pad, the Gamepad Test, the file manager and the Wi-Fi content server: the launcher has its own
  pad handling, file transfer and settings.

## The gamepad

`patches/3-tab5-gamepad.patch` teaches the launcher's Tab5 board about our pad. It is player one; a USB pad is
then player two (without ours, a USB pad is player one as before).

- **Rev B** (PCA9555 on the EXT socket) is found by itself. Not yet tried on a board: none exists.
- **Rev A** (twelve M5-Bus lines) needs its wiring in `/sd/config/tab5pad.txt`, one `BUTTON=Gnn` a line, buttons
  `UP DOWN LEFT RIGHT A B X Y L R SELECT START`. The board as routed, on a straight ribbon:

  ```
  UP=G16
  LEFT=G17
  L=G45
  RIGHT=G52
  SELECT=G7
  DOWN=G6
  START=G3
  B=G4
  R=G2
  Y=G48
  A=G47
  X=G51
  ```

  A hand-wired pad goes wherever the wires went. `tools/nvs2prefs.py` writes this file from the wiring a console
  was taught under the classic firmware, read out of a saved copy of its settings.

In the arcade app the menu is X or L (or a tap on the screen), the volume Y or R. HOME is Select+Start held for a
second, as in every launcher app.

## Layout

```
ports/arcade      the arcade app
ports/tabgame     the touch games' shell, and gen_about.py (About text from src/app.cpp)
ports/m5compat    what the games use of M5Unified and Arduino, on the launcher's SDK
ports/icons       make_icons.py and the icons it draws
hostsim           the launcher's SDK implemented on macOS, to run a game's own code with no board
patches           our changes to the launcher (below)
tools             build, stage the card, install, and drive a Tab5 over USB
tests             host tests for the gamepad decoding
```

Everything that is not ours to publish, or is only built, lives in a **work folder** outside the repository
(`TAB5_LAUNCHER_WORK`, default `~/Developer/p4-launcher-test`): the launcher's clone, `build/`, and `sdcard/`.
Game files (ROMs, WADs, paks) are your own, go on your own card, and are never committed.

## Patches to the launcher (against v0.32.0)

1. `1-trial-ramdisk-when-no-card.patch`: a RAM disk when no card mounts. Bench testing only; not built by default.
2. `2-st7121-panel-detection.patch`: on Tab5s with the ST7121 panel the launcher asked the touch chip once who it
   was, and on about every other restart the chip was not awake yet: black screen, no touch, sound playing. It
   now asks for up to 800 ms.
3. `3-tab5-gamepad.patch`: the gamepad, and a board flag that makes USB pads start at player two.
4. `4-tab5-accelerometer.patch`: the Tab5's BMI270, offered to apps as three more analog inputs
   (`PAPP_ANALOG_ACCEL_X/Y/Z`: 32768 is 0 g, 4096 to the g, the chip's own axes, which is what M5Unified gives on
   this board). Nothing touches the sensor until an app first asks; that first read loads the sensor's
   configuration file and takes a quarter of a second. Applies on top of patch 3.
5. `5-tab5-keyboard.patch`: M5Stack's clip-on Tab5 Keyboard (I2C 0x6D on the EXT socket, the bus the rev B pad
   shares), handed to apps as an ordinary keyboard: the same key events and HID usages a USB keyboard gives. Sym,
   Aa, Ctrl and Alt work as the keys' legends say (Sym gives the second legends, Aa is Shift, two quick Aa taps lock
   capitals). It can be clipped on and off while the launcher runs. Apps can also ask whether a keyboard is attached
   (`papp_input_keyboard()`, input interface version 3; older launchers answer no). Applies on top of patch 4.
6. `6-usb-host-for-apps.patch`: a new interface, USBH, giving apps raw access to devices on the USB host port
   that the launcher's own drivers do not take: list them, open one, claim an interface, then control, bulk and
   interrupt transfers (blocking, ended early by an exit request or an unplug). The HID and XInput drivers keep
   their interfaces. For the Multitool's T48 + One ROM app; `apps/usbh_test` exercises it. The port stays at
   full speed (the launcher limits it so hubs work). Applies on top of patch 5.
7. `7-fs-directories.patch`: the file interface's version 2: `stat` that tells folders from files, `rename`,
   `rmdir`, and listing a folder, with the SDK's C library's `stat()`, `rename()`, `rmdir()` and
   `opendir()`/`readdir()`/`closedir()` on top (they were missing or always said "a file"). Apps built with it still
   run on older launchers, where those calls fail as before. For the T48 + One ROM app's image picker. Applies on
   top of patch 6.

The patches are offered under the launcher's own MIT licence so they can go upstream.

## Build and install

Wants: ESP-IDF 5.5 (for the launcher and its RISC-V compiler), PlatformIO having built the classic firmware once
(the ports use its copy of M5GFX in `.pio/libdeps`), Python with `pyelftools`, `pyserial` and `esptool`.

```bash
git clone --branch v0.32.0 https://github.com/giltal/ESP32_P4_Launcher ~/Developer/p4-launcher-test/ESP32_P4_Launcher
launcher/tools/build_launcher.sh          # the launcher, with patches 2, 3 and 4
python launcher/ports/tabgame/build.py    # the six touch games
python launcher/ports/arcade/build.py     # the arcade app
launcher/tools/stage_sd.sh                # lays out sdcard/: apps, icons, word packs, sounds
launcher/tools/install.sh full            # a Tab5 on other firmware: SAVE ITS FLASH FIRST (see the script)
launcher/tools/install.sh card            # later: only the card's files
```

The launcher's own apps (`nes`, `gb`, `snes`, `doom`, `quake`) are built by its `apps/build_all.py`; `stage_sd.sh`
picks them up from its `build/apps`.

The card: games under `/sd/roms/<system>/` (`nes`, `gb`, `snes`, `arcade`, `doom/doom.wad`, `quake/id1/`), apps
under `/sd/apps/`, ours keeping their things under `/sd/tabulous/` (`packs/` with an `index.txt` naming its
files, because a launcher app cannot list a folder; `sfx/`; `*.prefs`).

A console moving over from the classic firmware keeps its settings and score tables:
`tools/nvs2prefs.py flash-backup-nvs.bin outdir` turns them into the `.prefs` files.

## Things found on the way that are worth knowing

- **The P4's low-power RAM is in the launcher's "internal" heap**, and it is about fifty times slower than the
  main internal RAM. The heap falls back to it when the main kind has no piece big enough, for a thread's stack
  as readily as for anything else. An app that takes a lot of internal RAM should make its threads first and
  check where its buffers landed (`ports/arcade/main.cpp`, `inSlowRam`).
- **The Tab5's panel shows 57 pictures a second.** Showing one waits for the panel, so an app that waits for
  every picture runs at 57, sound and all. The arcade app runs the machine at 60 and skips a picture when the
  last is still on its way.
- **Asking the touch panel where the finger is took 8 ms** on the console when this was first measured, and
  0.9 ms ever since the accelerometer was first set up. Which of the two changed it has not been established
  (the sensor shares the touch controller's bus and keeps its settings across a restart). Either way it is not
  something to do in a frame loop.
- Showing a 1280x720 surface takes 43 ms; the touch games show one only when the picture has changed.

## Two fixes to shared files

- `third_party/chips/namco.h`: the picture's line length can be set by the build, and the picture can live
  outside the machine's struct (`NAMCO_FB_SEPARATE`). The classic build sets neither and is unchanged.
- `src/pacman_ui.cpp`: the Ms. Pac-Man kit's program block is cleared before the program is copied in. What
  was left over above the program read as whatever the heap last held.
