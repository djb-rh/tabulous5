#include "buspad.h"

#include <Arduino.h>

#include "settings_store.h"

namespace tabulous {
namespace buspad {
namespace {

busmap::Wiring g_wiring = busmap::none();
bool g_wired = false;
bool g_begun = false;

}  // namespace

void begin() {
  for (int n = 0; n < busmap::kLines; n++) pinMode(busmap::kGpio[n], INPUT_PULLUP);
  g_begun = true;
  g_wiring = busmap::none();
  settings_store::loadBusWiring(&g_wiring);
  g_wired = busmap::valid(g_wiring) && busmap::assigned(g_wiring) > 0;
}

bool wired() { return g_wired; }

uint16_t lines() {
  if (!g_begun) return 0;
  uint16_t out = 0;
  for (int n = 0; n < busmap::kLines; n++) {
    if (digitalRead(busmap::kGpio[n]) == LOW) out |= (uint16_t)(1u << n);
  }
  return out;
}

uint16_t buttons() { return g_wired ? busmap::toButtons(lines(), g_wiring) : 0; }
uint8_t nes() { return busmap::toNes(buttons()); }
uint8_t stick() { return busmap::toStick(buttons()); }

busmap::Wiring wiring() { return g_wiring; }

void setWiring(const busmap::Wiring &w) {
  g_wiring = w;
  g_wired = busmap::valid(w) && busmap::assigned(w) > 0;
  settings_store::saveBusWiring(w);
}

}  // namespace buspad
}  // namespace tabulous
