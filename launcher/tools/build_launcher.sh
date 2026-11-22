#!/bin/bash
# Builds P4 Launcher for the Tab5 with our patches. Wants ESP-IDF 5.5's environment already set up (idf.py on
# the PATH) and the upstream launcher cloned as ESP32_P4_Launcher in the work folder, at the release the patches
# were cut against (v0.32.0):
#   git clone --branch v0.32.0 https://github.com/giltal/ESP32_P4_Launcher "$TAB5_LAUNCHER_WORK/ESP32_P4_Launcher"
# build_launcher.sh          patches 2 to 11, 13 and 14 (the screen fix, the gamepad, the accelerometer, the clip-on
#                            keyboard, USB host for apps, directories for apps, battery charging, random bytes,
#                            USB HID device for apps, Bluetooth pairing for apps, more than 64 apps on the home
#                            screen, a quiet home screen right after an app closes)
# build_launcher.sh hosted2  patch 12 as well: esp_hosted 2.12, for a Tab5 whose C6 runs 2.x firmware (then
#                            Bluetooth works; 1.4, the default, runs only Wi-Fi with such a C6)
# build_launcher.sh trial    patch 1 as well (a RAM disk when no card mounts: for bench testing only)
# Switching between 1.4 and 2.12 needs a fresh configuration: delete launcher/sdkconfig.m5stack_tab5 first.
set -e
here=$(cd "$(dirname "$0")" && pwd); work=${TAB5_LAUNCHER_WORK:-$HOME/Developer/p4-launcher-test}
cd "$work/ESP32_P4_Launcher"
patches="2-st7121-panel-detection 3-tab5-gamepad 4-tab5-accelerometer 5-tab5-keyboard 6-usb-host-for-apps 7-fs-directories 8-battery-charging 9-random-bytes 10-usb-hid-device-for-apps 11-ble-pairing-for-apps 13-more-apps 14-home-quiet-after-app"
for a in "$@"; do
  [ "$a" = trial ] && patches="1-trial-ramdisk-when-no-card $patches"
  [ "$a" = hosted2 ] && patches="$patches 12-tab5-esp-hosted-2"
done
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
    9-*) grep -q sys_random components/lx_loader/lx_services.c;;
    10-*) [ -f components/lx_usbd/lx_usbd.c ];;
    11-*) grep -q ble_gatt_register2 components/lx_loader/lx_svc_ble.c;;
    12-*) grep -q 'esp_hosted: "2' $b/idf_component.yml;;
    13-*) grep -q 'MAX_APPS      256' launcher/main/home.c;;
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
