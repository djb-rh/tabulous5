#!/bin/bash
# Builds P4 Launcher for the Tab5 with our patches. Wants ESP-IDF 5.5's environment already set up (idf.py on
# the PATH) and the upstream launcher cloned as ESP32_P4_Launcher in the work folder, at the release the patches
# were cut against (v0.32.0):
#   git clone --branch v0.32.0 https://github.com/giltal/ESP32_P4_Launcher "$TAB5_LAUNCHER_WORK/ESP32_P4_Launcher"
# build_launcher.sh          patches 2 to 6 (the screen fix, the gamepad, the accelerometer, the clip-on keyboard,
#                            USB host for apps, directories for apps, battery charging)
# build_launcher.sh trial    patch 1 as well (a RAM disk when no card mounts: for bench testing only)
set -e
here=$(cd "$(dirname "$0")" && pwd); work=${TAB5_LAUNCHER_WORK:-$HOME/Developer/p4-launcher-test}
cd "$work/ESP32_P4_Launcher"
patches="2-st7121-panel-detection 3-tab5-gamepad 4-tab5-accelerometer 5-tab5-keyboard 6-usb-host-for-apps 7-fs-directories 8-battery-charging"; [ "$1" = trial ] && patches="1-trial-ramdisk-when-no-card $patches"
# The patches build on one another (3 and 4 change the same files), so whether one is in is told by something
# only it adds, not by trying to take it out again.
b=boards/m5stack_tab5/board
applied() {
  case $1 in
    1-*) grep -q "sd_ramdisk\|RAM disk" $b/board.c;;
    2-*) grep -q "answered on try" $b/board_display.c;;
    3-*) [ -f $b/tab5pad.h ];;
    4-*) [ -f $b/board_imu.c ];;
    5-*) [ -f $b/board_keyboard.c ];;
    6-*) [ -f components/lx_loader/lx_svc_usbh.c ];;
    7-*) grep -q fs_opendir components/lx_loader/lx_services.c;;
    8-*) [ -f $b/board_power.c ];;
  esac
}
for p in $patches; do
  if applied $p; then echo "already applied: $p"
  else git apply "$here/../patches/$p.patch" && echo "applied: $p"; fi
done
cd launcher
idf.py -B build_m5stack_tab5 -DLX_BOARD=m5stack_tab5 build
echo "built $work/ESP32_P4_Launcher/launcher/build_m5stack_tab5/launcher_m5stack_tab5.bin"
