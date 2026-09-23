// The time of day, and where it comes from.
//
// The Tab5 has a real-time clock (an RX8130, kept running by the battery), and
// M5Unified sets the system time from it at boot. What it cannot say is
// whether that time is TRUE: a clock that was never set reads 2000. So the
// clock is only shown once the network has set it, through NTP, and that is
// remembered; from then on the RTC carries it between boots and the network
// only corrects the drift now and then.
//
// The network is the awkward part. The Wi-Fi co-processor's transport can be
// started once per boot and never again (see contentserver::stop), and while
// it is up it holds the internal RAM the emulators and the card need. So no
// session is ever started on the console's own initiative: the sync rides on
// the Wi-Fi editor's session whenever that is open, or on one the owner asks
// for with SYNC NOW. Between them the RTC carries the time.
#pragma once

#include <cstdint>
#include <ctime>

namespace tabulous {
namespace wallclock {

// Applies the zone and reads what was remembered. After M5.begin().
void begin(uint8_t zone);

// The zones on offer, as an index into a short list; see wallclock.cpp.
int zoneCount();
const char *zoneName(uint8_t zone);
void setZone(uint8_t zone);

// Whether the time can be believed, and if so what it is, locally.
bool trusted();
bool localTime(struct tm *out);
// When the network last set it, as a UTC epoch; 0 for never.
uint32_t lastSync();

// The network to use for a session of its own, started on request (the SYNC
// NOW button). While the Wi-Fi editor owns the radio, tick() syncs over its
// connection instead. False from startSync means it could not: no network
// configured, or the radio's one session this boot is spent (canSync).
void configure(const char *ssid, const char *password);
bool canSync();
bool startSync();
bool syncDue();

// What is going on, for the settings screen.
enum class State : uint8_t { Idle, Joining, Syncing, Done, Failed };
State state();

void tick(uint32_t now_ms);

}  // namespace wallclock
}  // namespace tabulous
