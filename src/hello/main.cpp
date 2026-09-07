// Minimal serial liveness test. No M5, no display, no PSRAM — if this does not
// print, the problem is the USB-CDC path or the host, not the application.
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  // Deliberately NOT setTxTimeoutMs(0): the default 100 ms timeout blocks
  // until the host drains, which is exactly what we want while proving the
  // link works at all.
}

void loop() {
  static uint32_t n = 0;
  Serial.printf("hello %lu millis=%lu\n", (unsigned long)n++, (unsigned long)millis());
  delay(250);
}
