#!/bin/bash
# Host tests for the gamepad decoding that patches/3-tab5-gamepad.patch adds to the launcher (tab5pad.h is in the
# patched clone, which lives in the work folder).
set -e
here=$(cd "$(dirname "$0")" && pwd); work=${TAB5_LAUNCHER_WORK:-$HOME/Developer/p4-launcher-test}
mkdir -p "$work/build/tests"
cc -Wall -Wextra -I"$work/ESP32_P4_Launcher/boards/m5stack_tab5/board" -o "$work/build/tests/test_tab5pad" "$here/test_tab5pad.c"
"$work/build/tests/test_tab5pad"
