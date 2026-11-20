#!/bin/bash
# Builds P4 Launcher for the Tab5 with our patches. Wants ESP-IDF 5.5's environment already set up (idf.py on
# the PATH) and the upstream launcher cloned as ESP32_P4_Launcher in the work folder, at the release the patches
# were cut against (v0.32.0):
#   git clone --branch v0.32.0 https://github.com/giltal/ESP32_P4_Launcher "$TAB5_LAUNCHER_WORK/ESP32_P4_Launcher"
# build_launcher.sh          patches 2 and 3 (the screen fix and the gamepad)
# build_launcher.sh trial    patch 1 as well (a RAM disk when no card mounts: for bench testing only)
set -e
here=$(cd "$(dirname "$0")" && pwd); work=${TAB5_LAUNCHER_WORK:-$HOME/Developer/p4-launcher-test}
cd "$work/ESP32_P4_Launcher"
patches="2-st7121-panel-detection 3-tab5-gamepad"; [ "$1" = trial ] && patches="1-trial-ramdisk-when-no-card $patches"
for p in $patches; do
  f="$here/../patches/$p.patch"
  if git apply --reverse --check "$f" 2>/dev/null; then echo "already applied: $p"
  else git apply "$f" && echo "applied: $p"; fi
done
cd launcher
idf.py -B build_m5stack_tab5 -DLX_BOARD=m5stack_tab5 build
echo "built $work/ESP32_P4_Launcher/launcher/build_m5stack_tab5/launcher_m5stack_tab5.bin"
