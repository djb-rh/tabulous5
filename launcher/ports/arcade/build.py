#!/usr/bin/env python3
"""Builds ports/arcade as arcade.papp, from the console's own sources in this repository."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
# The repository holds the sources; everything that is not ours to publish or is only built here lives in a work
# folder outside it: the upstream launcher's clone (ESP32_P4_Launcher), build/, and sdcard/ with its ROMs.
WORK = os.path.expanduser(os.environ.get("TAB5_LAUNCHER_WORK", "~/Developer/p4-launcher-test"))
LAUNCHER = os.path.join(WORK, "ESP32_P4_Launcher")
T5 = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(LAUNCHER, "papp-sdk", "tools"))
import papp_build  # noqa: E402

import subprocess
CXX = ["-std=gnu++17", "-D_Static_assert=static_assert", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics"]
OUT = os.path.join(WORK, "build", "arcade")
INC = [os.path.join(T5, "src"), os.path.join(T5, "third_party", "chips"), os.path.join(T5, "third_party", "z80")]
srcs = [os.path.join(HERE, "main.cpp"), os.path.join(T5, "src", "arcrom.cpp"), os.path.join(T5, "src", "fourway.cpp")]

# The machine itself, the Z80 and the board with its picture and sound, goes in internal RAM (the launcher's FAST
# region) with its tables: from PSRAM it ran at 56 frames a second on the Tab5. The SDK's way is to tag each
# function PAPP_FAST; these are somebody else's files, so their objects' sections are renamed instead.
os.makedirs(OUT, exist_ok=True)
gcc = papp_build.find_tool("riscv32-esp-elf-gcc")
objcopy = gcc[:-3] + "objcopy"
fast_objs = []
for src in (os.path.join(T5, "third_party", "chips", "chips_impl.c"), os.path.join(T5, "third_party", "z80", "z80_sz.c")):
    obj = os.path.join(OUT, "fast_" + os.path.splitext(os.path.basename(src))[0] + ".o")
    subprocess.check_call([gcc] + papp_build.CFLAGS + ["-O2", "-DNAMCO_PACMAN", "-DNAMCO_FB_SEPARATE", "-DNAMCO_FRAMEBUFFER_WIDTH=288"] + ["-I" + i for i in INC] + ["-c", src, "-o", obj])
    subprocess.check_call([objcopy, "--prefix-alloc-sections=.fast", obj])
    fast_objs.append(obj)

papp_build.build("arcade", srcs, includes=INC, libc=True, out_dir=OUT, extra_cflags=["-O2", "-DNAMCO_PACMAN", "-DNAMCO_FB_SEPARATE", "-DNAMCO_FRAMEBUFFER_WIDTH=288"],
                 per_file_cflags={s: CXX for s in srcs if s.endswith(".cpp")}, extra_ldflags=fast_objs)
