#!/bin/sh
# Builds the release files into dist/ and the web installer into site/.
# There are three images, because the big engines do not fit in one (see
# platformio.ini):
#   tabulous5-<ver>.bin        everything except Doom and Quake
#   tabulous5-doom-<ver>.bin   Doom, without the SNES
#   tabulous5-quake-<ver>.bin  Quake, without the SNES or Doom
# Each is a merged image (bootloader 0x2000, partitions 0x8000, app 0x10000),
# flashed at offset 0. site/ holds index.html, a manifest for each image,
# home.png and both .bin files, which is what the gh-pages branch serves.
# The LittleFS image is deliberately NOT built or shipped: data/ holds ROMs
# that are not ours to distribute, and the firmware sets its packs up on
# first boot anyway. Usage: tools/release.sh v1.4.0
set -e
ver=${1:?usage: tools/release.sh <version>}
cd "$(dirname "$0")/.."
~/.platformio/penv/bin/pio run -e tab5 -e tab5-doom -e tab5-quake
rm -rf dist site && mkdir -p dist site
cp .pio/build/tab5/firmware.factory.bin "dist/tabulous5-$ver.bin"
cp .pio/build/tab5-doom/firmware.factory.bin "dist/tabulous5-doom-$ver.bin"
cp .pio/build/tab5-quake/firmware.factory.bin "dist/tabulous5-quake-$ver.bin"
cp dist/*.bin site/
sed -e "s/@VER@/$ver/g" web-installer/index.html > site/index.html
sed -e "s/@VER@/$ver/g" -e "s/@NAME@/Tabulous5/" -e "s/@FILE@/tabulous5-$ver.bin/" web-installer/manifest.json > site/manifest.json
sed -e "s/@VER@/$ver/g" -e "s/@NAME@/Tabulous5 (Doom)/" -e "s/@FILE@/tabulous5-doom-$ver.bin/" web-installer/manifest.json > site/manifest-doom.json
sed -e "s/@VER@/$ver/g" -e "s/@NAME@/Tabulous5 (Quake)/" -e "s/@FILE@/tabulous5-quake-$ver.bin/" web-installer/manifest.json > site/manifest-quake.json
cp assets/home.png site/home.png
ls -la dist site
