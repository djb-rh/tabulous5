#!/bin/bash
# Puts our launcher build and our apps on a Tab5 over USB.
#   install.sh app  [PORT]   the launcher alone (a Tab5 that already runs it), then the card's files
#   install.sh full [PORT]   bootloader, partition table and launcher: for a Tab5 running something else.
#                            THIS REPLACES WHAT IS ON IT. Save it first:
#                              esptool --chip esp32p4 -p PORT read-flash 0 0x1000000 backup.bin
#   install.sh card [PORT]   only the card's files: apps/, config/ and tabulous/ (no ROMs, nothing is deleted)
# Wants esptool and pyserial (PlatformIO's Python has both: ~/.platformio/penv/bin).
set -e
here=$(cd "$(dirname "$0")" && pwd); work=${TAB5_LAUNCHER_WORK:-$HOME/Developer/p4-launcher-test}
what=${1:-card}; port=${2:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)}
[ -n "$port" ] || { echo "no Tab5 found on USB"; exit 1; }
b="$work/ESP32_P4_Launcher/launcher/build_m5stack_tab5"
py=${PYTHON:-$HOME/.platformio/penv/bin/python}; esptool="$(dirname "$py")/esptool"
case $what in
  full) "$esptool" --chip esp32p4 -p "$port" -b 921600 write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB \
          0x2000 "$b/bootloader/bootloader.bin" 0x8000 "$b/partition_table/partition-table.bin" \
          0xf000 "$b/ota_data_initial.bin" 0x20000 "$b/launcher_m5stack_tab5.bin";;
  app)  "$esptool" --chip esp32p4 -p "$port" -b 921600 write-flash 0x20000 "$b/launcher_m5stack_tab5.bin";;
  card) ;;
  *) echo "install.sh app|full|card [PORT]"; exit 1;;
esac
[ $what = card ] || "$py" -c "import time; time.sleep(9)"       # the launcher starting up
PAPP_PORT="$port" "$py" "$here/trial.py" push apps config tabulous
