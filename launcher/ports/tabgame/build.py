#!/usr/bin/env python3
"""Builds the console's touch games as .papp files:  build.py [mines|sudoku|solitaire|phrase|fivehead|joshua ...]  (default: all).

The games' sources come straight from this repository's src/, LovyanGFX from its M5GFX (.pio/libdeps, fetched by
a PlatformIO build of the console firmware); only main.cpp here and ../m5compat are new."""
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
# The repository holds the sources; everything that is not ours to publish or is only built here lives in a work
# folder outside it: the upstream launcher's clone (ESP32_P4_Launcher), build/, and sdcard/ with its ROMs.
WORK = os.path.expanduser(os.environ.get("TAB5_LAUNCHER_WORK", "~/Developer/p4-launcher-test"))
ROOT = WORK
T5 = os.path.abspath(os.path.join(HERE, "..", "..", ".."))        # this repository: the games' own sources are in src/
GFX = os.path.join(T5, ".pio", "libdeps", "tab5", "M5GFX", "src")
sys.path.insert(0, os.path.join(ROOT, "ESP32_P4_Launcher", "papp-sdk", "tools"))
import papp_build  # noqa: E402

# key: (define, app folder, the game's name in the console's menu table, its own source files)
GAMES = {"mines": ("MINES", "minesweeper", "Minesweeper", ["minesweeper", "minesweeper_ui"]),
         "sudoku": ("SUDOKU", "sudoku", "Sudoku", ["sudoku", "sudoku_ui"]),
         "solitaire": ("SOLITAIRE", "solitaire", "Solitaire", ["solitaire", "solitaire_ui"]),
         "phrase": ("PHRASE", "phrasecraze", "PhraseCraze", ["phrase_game", "round_timer", "phrase_ui", "packpicker", "confetti"]),
         "fivehead": ("FIVEHEAD", "fivehead", "FiveHead", ["fivehead", "fivehead_ui", "packpicker"]),
         "joshua": ("JOSHUA", "joshua", "Joshua", ["joshua_ui"])}
SHARED = ["uikit", "theme", "textentry", "scoreboard", "highscores", "glyphs", "audio", "wav", "settings_store", "content", "pack"]
CXX = ["-std=gnu++17", "-include", "cstdint", "-include", "cmath", "-D_Static_assert=static_assert", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics", "-w"]
LGFX = ["LGFXBase", "LGFX_Sprite", "lgfx_fonts", "LGFX_Button", "panel/Panel_Device", "misc/pixelcopy",
        "misc/common_function", "misc/SpriteBuffer", "misc/DividedFrameBuffer"]
UTIL = ["lgfx_miniz", "lgfx_pngle", "lgfx_qoi", "lgfx_qrcode", "lgfx_tjpgd"]


def build(which):
    define, name, title, files = GAMES[which]
    gen = os.path.join(ROOT, "build", name, "gen"); os.makedirs(gen, exist_ok=True)
    subprocess.check_call([sys.executable, os.path.join(HERE, "gen_about.py"), title, os.path.join(gen, "about_gen.h")])
    srcs = [os.path.join(HERE, "main.cpp"), os.path.join(HERE, "..", "m5compat", "m5compat.cpp"), os.path.join(HERE, "..", "m5compat", "m5sound.cpp")]
    srcs.append(os.path.join(ROOT, "ESP32_P4_Launcher", "apps", "common", "odroid", "cxx_papp.c"))   # the launcher's own: the pthread subset libstdc++ wants
    srcs += [os.path.join(T5, "src", f + ".cpp") for f in SHARED + files]
    srcs += [os.path.join(GFX, "lgfx", "v1", f + ".cpp") for f in LGFX]
    srcs += [os.path.join(GFX, "lgfx", "utility", f + ".c") for f in UTIL]
    stubs = os.path.join(HERE, "font_stubs.c")
    if os.path.isfile(stubs):
        srcs.append(stubs)
    papp_build.build(name, srcs, includes=[os.path.join(HERE, "..", "m5compat", "include"), GFX, os.path.join(T5, "src"), gen],
                     libc=True, out_dir=os.path.join(ROOT, "build", name), version_dir=HERE,
                     extra_cflags=["-O2", "-D__linux__", "-DLGFX_LINUX_FB", "-DTABGAME_" + define, "-w"],
                     per_file_cflags={s: CXX for s in srcs if s.endswith(".cpp")},
                     extra_libs=["-lstdc++", "-lsupc++"])


for g in (sys.argv[1:] or list(GAMES)):
    build(g)
