#ifndef ANEMOIA_DEBUG_H
#define ANEMOIA_DEBUG_H

// Replaces upstream's debug.h, which dumps the reference board's pin map and
// needs TFT_eSPI. The core only uses LOG and LOGF.
//
// OFF by default, and it should stay that way: these sit on per-frame paths,
// and a Serial write per frame on this device blocks long enough to change the
// behaviour being logged. Define ANEMOIA_DEBUG only for a deliberate session.

#include <Arduino.h>

#ifdef ANEMOIA_DEBUG
#define LOG(msg) Serial.println(msg)
#define LOGF(fmt, ...) Serial.printf(fmt, __VA_ARGS__)
#else
#define LOG(msg) ((void)0)
#define LOGF(fmt, ...) ((void)0)
#endif

#endif
