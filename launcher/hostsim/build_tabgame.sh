#!/bin/bash
# build_tabgame.sh MINES|SUDOKU|SOLITAIRE|PHRASE|FIVEHEAD|JOSHUA: one of the console's touch games against the Mac stand-in for the launcher.
set -e
which=$1; here=$(cd "$(dirname "$0")" && pwd); T5=$(cd "$here/../.." && pwd); G=$T5/.pio/libdeps/tab5/M5GFX/src; P=$here/../ports
work=${TAB5_LAUNCHER_WORK:-$HOME/Developer/p4-launcher-test}; L=$work/ESP32_P4_Launcher; B=$work/build/hostsim   # the launcher's clone and our build output live outside the repository
out=$B/tab_$which; mkdir -p $out/out
INC="-I$here/include -I$here/include/macstub -I$P/m5compat/include -I$G -I$T5/src -I$L/components/papp_abi/include"
DEF="-D__linux__ -DLGFX_LINUX_FB -DTABGAME_$which -DPAPP_SDK_HEADER=\"$L/papp-sdk/include/papp/papp.h\""
CXX="clang++ -std=gnu++17 -O1 -w $DEF $INC"
# LovyanGFX's core, once
if [ ! -f $B/lgfx.a ]; then
  mkdir -p $B/lgfxo
  for f in LGFXBase LGFX_Sprite lgfx_fonts LGFX_Button panel/Panel_Device misc/pixelcopy misc/common_function misc/SpriteBuffer misc/DividedFrameBuffer; do
    $CXX -c $G/lgfx/v1/$f.cpp -o $B/lgfxo/$(basename $f).o
  done
  for f in lgfx_miniz lgfx_pngle lgfx_qoi lgfx_qrcode lgfx_tjpgd; do clang -O1 -w $INC -c $G/lgfx/utility/$f.c -o $B/lgfxo/$f.o; done
  # the CJK fonts are 118 MB of source nobody here draws with: one byte each stands in for them
  nm -u $B/lgfxo/lgfx_fonts.o | sed -n 's/^_\(lgfx_[A-Za-z0-9_]*\)$/const unsigned char \1[1] = {0};/p' > $B/lgfxo/font_stubs.c
  clang -c $B/lgfxo/font_stubs.c -o $B/lgfxo/font_stubs.o
  ar rcs $B/lgfx.a $B/lgfxo/*.o
fi
case $which in
  MINES) game="minesweeper minesweeper_ui"; name=Minesweeper;;
  SUDOKU) game="sudoku sudoku_ui"; name=Sudoku;;
  SOLITAIRE) game="solitaire solitaire_ui"; name=Solitaire;;
  PHRASE) game="phrase_game round_timer phrase_ui packpicker confetti"; name=PhraseCraze;;
  FIVEHEAD) game="fivehead fivehead_ui packpicker"; name=FiveHead;;
  JOSHUA) game="joshua_ui"; name=Joshua;;
esac
python3 $P/tabgame/gen_about.py $name $out/about_gen.h
emb=""; [ "$which" = PHRASE ] && emb=packs; [ "$which" = FIVEHEAD ] && emb=packs
python3 $P/tabgame/gen_embedded.py $out/embedded_gen.c $emb && clang -O1 -c $out/embedded_gen.c -o $out/embedded_gen.o
clang -O1 -w $INC -DPAPP_SDK_HEADER="\"$L/papp-sdk/include/papp/papp.h\"" -c $here/papp_host.c -o $out/host.o
$CXX -I$out -include host_main.h -c $P/tabgame/main.cpp -o $out/main.o
$CXX -include host_main.h -c $P/m5compat/m5compat.cpp -o $out/m5compat.o
$CXX -include host_main.h -c $P/m5compat/m5sound.cpp -o $out/m5sound.o
objs="$out/host.o $out/main.o $out/m5compat.o $out/m5sound.o $out/embedded_gen.o"
for f in uikit theme textentry scoreboard highscores glyphs audio wav settings_store content pack orientation $game; do $CXX -c $T5/src/$f.cpp -o $out/$f.o; objs="$objs $out/$f.o"; done
clang++ $objs $B/lgfx.a -o $out/app
echo "built $out/app"
