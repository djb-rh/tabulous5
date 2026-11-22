#!/bin/bash
# Builds P4 Launcher for the Tab5 with our patches. Wants ESP-IDF 5.5's environment already set up (idf.py on
# the PATH) and the upstream launcher cloned as ESP32_P4_Launcher in the work folder, at the release the patches
# were cut against (v0.34.0):
#   git clone --branch v0.34.0 https://github.com/giltal/ESP32_P4_Launcher "$TAB5_LAUNCHER_WORK/ESP32_P4_Launcher"
# build_launcher.sh          patches 3 and 14 (the gamepad; a quiet home screen right after an app closes).
#                            Everything else we used to patch in is part of the launcher since 0.34.0.
# build_launcher.sh trial    patch 1 as well (a RAM disk when no card mounts: for bench testing only)
# Coming from a 0.32 build in the same folder: delete launcher/sdkconfig.m5stack_tab5 and launcher/build_m5stack_tab5
# first (0.34 moved the Tab5 to esp_hosted 2.x, which wants a fresh configuration).
set -e
here=$(cd "$(dirname "$0")" && pwd); work=${TAB5_LAUNCHER_WORK:-$HOME/Developer/p4-launcher-test}
cd "$work/ESP32_P4_Launcher"
patches="3-tab5-gamepad 14-home-quiet-after-app"
for a in "$@"; do
  [ "$a" = trial ] && patches="1-trial-ramdisk-when-no-card $patches"
done
# Whether a patch is in is told by something only it adds, not by trying to take it out again.
b=boards/m5stack_tab5/board
applied() {
  case $1 in
    1-*) grep -q "sd_ramdisk\|RAM disk" $b/board.c;;
    3-*) [ -f $b/tab5pad.h ];;
    14-*) grep -q BACK_QUIET_US launcher/main/home.c;;
  esac
}
for p in $patches; do
  if applied $p; then echo "already applied: $p"
  else git apply "$here/../patches/$p.patch" && echo "applied: $p"; fi
done
cd launcher
idf.py -B build_m5stack_tab5 -DLX_BOARD=m5stack_tab5 build
echo "built $work/ESP32_P4_Launcher/launcher/build_m5stack_tab5/launcher_m5stack_tab5.bin"
