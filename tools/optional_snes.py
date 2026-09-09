"""Builds the SNES core in, but only if the builder has fetched it.

The core is not in this repository and is not shipped with it: its licensing
is unsettled (the copy in circulation carries a GPL-2-or-later notice, while
the snes9x code it descends from is under a non-commercial licence that is not
a free software licence). Rather than take a position on someone else's
tangle, this project ships the glue and leaves the core to whoever builds it.

So: drop the sources in third_party/snes9x/ (or leave an archive of them
there) and the SNES appears in the launcher. Do nothing and everything else
builds exactly as before, with no SNES entry. See third_party/snes9x/README.md.
"""

import glob
import os
import shutil
import subprocess
import tarfile
import zipfile

Import("env")  # noqa: F821  (PlatformIO injects this)

ROOT = env.subst("$PROJECT_DIR")  # noqa: F821
CORE = os.path.join(ROOT, "third_party", "snes9x")
PATCHES = os.path.join(ROOT, "patches", "snes9x")
MARKER = os.path.join(CORE, ".patched")

# One file that only the real core has, used to tell "fetched" from "empty".
SENTINEL = "cpuops.c"


def say(message):
    print("snes9x: %s" % message)


def find_core_dir(top):
    """The directory holding the sources, however deeply an archive nested it."""
    for path, _dirs, files in os.walk(top):
        if SENTINEL in files:
            return path
    return None


def unpack_archive():
    """Extracts the first archive found in third_party/snes9x/, if the sources
    are not already unpacked beside it."""
    archives = sorted(
        glob.glob(os.path.join(CORE, "*.zip"))
        + glob.glob(os.path.join(CORE, "*.tar.gz"))
        + glob.glob(os.path.join(CORE, "*.tgz"))
    )
    if not archives:
        return
    archive = archives[0]
    into = os.path.join(CORE, "_unpacked")
    if os.path.isdir(into):
        return
    say("unpacking %s" % os.path.basename(archive))
    os.makedirs(into, exist_ok=True)
    try:
        if archive.endswith(".zip"):
            with zipfile.ZipFile(archive) as z:
                z.extractall(into)
        else:
            with tarfile.open(archive) as t:
                t.extractall(into)
    except Exception as exc:  # a half-unpacked tree is worse than none
        shutil.rmtree(into, ignore_errors=True)
        say("could not unpack %s: %s" % (os.path.basename(archive), exc))


def apply_patches(core_dir):
    """Applies patches/snes9x/*.patch once, marking the tree when it is done.

    The core expects a frontend this project does not have, so it needs small
    edits to build here. Keeping them as patches rather than a forked copy is
    what lets the builder fetch any compatible version of the sources.
    """
    patches = sorted(glob.glob(os.path.join(PATCHES, "*.patch")))
    if not patches or os.path.exists(MARKER):
        return True
    for patch in patches:
        say("applying %s" % os.path.basename(patch))
        result = subprocess.run(
            ["patch", "-p1", "-N", "-r", os.devnull, "-i", patch],
            cwd=core_dir, capture_output=True, text=True,
        )
        output = result.stdout + result.stderr
        # `patch -N` reports failure when every hunk is already in place, which
        # is what a rebuild against an already-patched tree looks like.
        already = "previously applied" in output or "Reversed (or previously" in output
        if result.returncode != 0 and not already:
            say("patch failed: %s" % output.strip())
            return False
    with open(MARKER, "w") as f:
        f.write("patches applied\n")
    return True


unpack_archive()
core_dir = find_core_dir(CORE) if os.path.isdir(CORE) else None

if not core_dir:
    say("not found, building without SNES (see third_party/snes9x/README.md)")
else:
    if apply_patches(core_dir):
        say("found in %s, building it in" % os.path.relpath(core_dir, ROOT))
        env.Append(  # noqa: F821
            CPPPATH=[core_dir],
            CPPDEFINES=[("HAVE_SNES", "1"), "NO_ZERO_LUT"],
            # RISC-V puts small globals in a window one register can reach --
            # 4 KB in total, for the whole firmware. The framework's own
            # libraries already fill most of it, and the core's extra 548
            # bytes are enough to push it over, at which point the link fails
            # with "--enable-non-contiguous-regions discards section .sdata..."
            # for every small global in the program, not just the core's.
            # Turning the optimisation off for everything this project
            # compiles leaves the window to the prebuilt libraries. It costs a
            # slightly longer instruction to reach a global, and nothing else.
            CCFLAGS=["-msmall-data-limit=0"],
            # The core has globals the RISC-V compiler wants to put in the
            # small-data section, and this linker script cannot place a
            # .sdata section coming from an ordinary object file -- it fails
            # with "--enable-non-contiguous-regions discards section". The
            # core's own ESP-IDF build solves this with a linker fragment,
            # which is not available here; turning the small-data
            # optimisation off does the same job. It applies to this
            # project's sources too, and costs them nothing but a slightly
            # longer instruction to reach a global.
        )
        # SuperFX is left out on purpose: the stub is what the working ESP32
        # builds of this core use, and the full interpreter does not fit the
        # frame budget here anyway.
        skip = ("fxinst.c", "fxemu.c", "unzip.c", "main.c")
        sources = [
            os.path.basename(p)
            for p in sorted(glob.glob(os.path.join(core_dir, "*.c")))
            if os.path.basename(p) not in skip
        ]
        # A library, not loose object files. Linked as objects, the core's
        # per-symbol sections have to be placed one by one and the link fails
        # with "--enable-non-contiguous-regions discards section .sdata..." --
        # not for want of memory (the firmware uses a tenth of it) but because
        # of how they are laid out. Archived first, the core links the way the
        # framework's own libraries do, and only the parts actually used are
        # pulled in.
        lib = env.BuildLibrary(  # noqa: F821
            os.path.join("$BUILD_DIR", "snes9x"), core_dir,
            " ".join("+<%s>" % s for s in sources),
        )
        env.Append(LIBS=[lib])  # noqa: F821
    else:
        say("patches did not apply, building without SNES")
