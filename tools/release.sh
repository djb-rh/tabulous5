#!/bin/sh
# Builds the release files into dist/ and the web installer into site/:
#   tabulous5-<ver>.bin   merged image (bootloader 0x2000, partitions 0x8000,
#                         app 0x10000), flashed at offset 0
#   site/                 index.html, manifest.json, home.png and the .bin,
#                         which is what the gh-pages branch serves
# The LittleFS image is deliberately NOT built or shipped: data/ holds ROMs
# that are not ours to distribute, and the firmware sets its packs up on
# first boot anyway. Usage: tools/release.sh v1.3.0
set -e
ver=${1:?usage: tools/release.sh <version>}
cd "$(dirname "$0")/.."
~/.platformio/penv/bin/pio run -e tab5
rm -rf dist site && mkdir -p dist site
cp .pio/build/tab5/firmware.factory.bin "dist/tabulous5-$ver.bin"
cp "dist/tabulous5-$ver.bin" site/
sed -e "s/@VER@/$ver/g" web-installer/index.html > site/index.html
sed -e "s/@VER@/$ver/g" web-installer/manifest.json > site/manifest.json
cp assets/home.png site/home.png
ls -la dist site
