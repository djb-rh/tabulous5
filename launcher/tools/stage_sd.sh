#!/bin/bash
# Lays out sdcard/ (in the work folder) as a console's card: the launcher's starter files, its apps and ours, our
# games' word packs and sounds. `stage_sd.sh roms` also copies a sampling of games for a test card: those are the
# owner's own files, they stay on this Mac and that card, and nothing under sdcard/ is ever committed.
set -e
here=$(cd "$(dirname "$0")" && pwd); T5=$(cd "$here/../.." && pwd); P=$here/../ports
cd "${TAB5_LAUNCHER_WORK:-$HOME/Developer/p4-launcher-test}"; L=ESP32_P4_Launcher; S=sdcard
R=${TAB5_ROMS:-}                 # for `stage_sd.sh roms`: a folder of your own game files (SNES/, DOOM/, QUAKE/)
mkdir -p $S/config $S/roms/{nes,gb,snes,doom,quake/id1,arcade} $S/tabulous $S/apps
cp $L/SDcard/config/gamepads.txt $S/config/
app() { [ -f "$2" ] || { echo "not built yet: $2"; return 0; }; mkdir -p "$S/apps/$1"; cp "$2" "$S/apps/$1/$1.papp"; [ -n "$3" ] && [ -f "$3" ] && cp "$3" "$S/apps/$1/icon.png"; true; }
for a in nes gb gbc snes doom quake; do app $a $L/build/apps/$a/$a.papp $L/build/apps/$a/icon.png; done
for a in arcade minesweeper sudoku solitaire phrasecraze fivehead joshua; do app $a build/$a/$a.papp $P/icons/$a/icon.png; done   # icons: ports/icons/make_icons.py
# What the console keeps in its flash filesystem, for our games: the word packs and the sound samples. A launcher
# app cannot list a folder, so packs/ carries an index of its files.
mkdir -p $S/tabulous/packs $S/tabulous/sfx
cp $T5/data/packs/*.txt $S/tabulous/packs/; cp $T5/data/sfx/*.wav $S/tabulous/sfx/
rm -f $S/tabulous/packs/index.txt; (cd $S/tabulous/packs && ls *.txt | grep -v '^index.txt$' > index.tmp && mv index.tmp index.txt)
if [ "$1" = roms ]; then
[ -n "$R" ] || { echo "set TAB5_ROMS to the folder your own game files are in"; exit 1; }
cp "$T5/data/nes/Super Mario Bros. (World).nes" "$T5/data/nes/Super Mario Bros. 3 (USA).nes" "$T5/data/nes/Mega Man 2 (USA).nes" $S/roms/nes/
cp "$T5/data/gb/Tetris (World) (Rev 1).gb" "$T5/data/gb/Legend of Zelda, The - Link's Awakening (USA, Europe).gb" $S/roms/gb/
cp "$R/SNES/Super Mario World (USA).zip" "$R/SNES/F-Zero (USA).zip" $S/roms/snes/
cp "$R/DOOM/DOOM.WAD" $S/roms/doom/doom.wad
cp "$R/QUAKE/from-iso/id1/pak0.pak" "$R/QUAKE/from-iso/id1/pak1.pak" $S/roms/quake/id1/
fi
find $S -type f | sort | while read f; do printf "%10d  %s\n" $(stat -f %z "$f") "$f"; done
du -sh $S
