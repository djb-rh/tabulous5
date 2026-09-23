#include "wallclock.h"

#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include <cstdlib>
#include <cstring>

#include "contentserver.h"
#include "snes_ui.h"

namespace tabulous {
namespace wallclock {
namespace {

struct Zone {
  const char *name;
  const char *posix;
};

// POSIX rules rather than a database: the whole tz database would be
// megabytes for a handful of places this is ever going to be.
const Zone kZones[] = {
    {"Eastern (US)", "EST5EDT,M3.2.0,M11.1.0"},
    {"Central (US)", "CST6CDT,M3.2.0,M11.1.0"},
    {"Mountain (US)", "MST7MDT,M3.2.0,M11.1.0"},
    {"Arizona", "MST7"},
    {"Pacific (US)", "PST8PDT,M3.2.0,M11.1.0"},
    {"Alaska", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"Hawaii", "HST10"},
    {"UTC", "UTC0"},
    {"UK", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Central Europe", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Japan", "JST-9"},
    {"Eastern Australia", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
};
constexpr int kZoneCount = (int)(sizeof(kZones) / sizeof(kZones[0]));

constexpr const char *kNamespace = "tab5time";
Preferences g_prefs;

uint8_t g_zone = 0;
bool g_trusted = false;
uint32_t g_last_sync = 0;

State g_state = State::Idle;
uint32_t g_state_ms = 0;
bool g_own_radio = false;      // this module joined; it disconnects
bool g_session_synced = false; // a sync has completed this boot
const char *g_ssid = nullptr;
const char *g_password = nullptr;

constexpr uint32_t kResyncAfterS = 30u * 24 * 3600;
constexpr uint32_t kJoinMs = 30000;
constexpr uint32_t kSyncMs = 30000;
// Anything earlier than this is a clock that was never set.
constexpr int kSaneYear = 2025;

bool rtcSane() {
  if (!M5.Rtc.isEnabled()) return false;
  const m5::rtc_datetime_t dt = M5.Rtc.getDateTime();
  return dt.date.year >= kSaneYear;
}

void applyZone() {
  setenv("TZ", kZones[g_zone].posix, 1);
  tzset();
}

void startSntp() {
  // Restarting SNTP is how it is told to ask again now rather than at its
  // next hourly poll.
  if (esp_sntp_enabled()) esp_sntp_stop();
  configTzTime(kZones[g_zone].posix, "pool.ntp.org", "time.nist.gov", "time.google.com");
  g_state = State::Syncing;
  g_state_ms = millis();
  Serial.println("[time] asking the network");
}

void recordSync() {
  const time_t now = time(nullptr);
  struct tm utc;
  gmtime_r(&now, &utc);
  if (M5.Rtc.isEnabled()) M5.Rtc.setDateTime(&utc);
  g_trusted = true;
  g_last_sync = (uint32_t)now;
  g_session_synced = true;
  if (g_prefs.begin(kNamespace, false)) {
    g_prefs.putUChar("trusted", 1);
    g_prefs.putUInt("last_sync", g_last_sync);
    g_prefs.end();
  }
  struct tm local;
  localtime_r(&now, &local);
  char text[48];
  strftime(text, sizeof(text), "%a %b %e %H:%M:%S %Z", &local);
  Serial.printf("[time] set from the network: %s\n", text);
}

void endSession(State how) {
  if (g_own_radio) {
    // Powered off, as the editor does: an idle radio keeps every byte it
    // took. That was the radio's one session this boot.
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    contentserver::noteRadioSpent();
    g_own_radio = false;
    snes_ui::reserveWorkRamEarly();
  }
  g_state = how;
}

}  // namespace

void begin(uint8_t zone) {
  g_zone = zone < kZoneCount ? zone : 0;
  applyZone();
  if (g_prefs.begin(kNamespace, true)) {
    const bool remembered = g_prefs.getUChar("trusted", 0) != 0;
    g_last_sync = g_prefs.getUInt("last_sync", 0);
    g_prefs.end();
    g_trusted = remembered && rtcSane();
  }
  Serial.printf("[time] rtc=%d sane=%d trusted=%d last_sync=%lu zone=%s\n",
                (int)M5.Rtc.isEnabled(), (int)rtcSane(), (int)g_trusted,
                (unsigned long)g_last_sync, kZones[g_zone].name);
}

int zoneCount() { return kZoneCount; }
const char *zoneName(uint8_t zone) { return kZones[zone < kZoneCount ? zone : 0].name; }

void setZone(uint8_t zone) {
  g_zone = zone < kZoneCount ? zone : 0;
  applyZone();
}

bool trusted() { return g_trusted; }

bool localTime(struct tm *out) {
  if (!g_trusted || !out) return false;
  const time_t now = time(nullptr);
  localtime_r(&now, out);
  return true;
}

uint32_t lastSync() { return g_last_sync; }

bool syncDue() {
  if (!g_trusted) return true;
  const uint32_t now = (uint32_t)time(nullptr);
  return now - g_last_sync > kResyncAfterS;
}

State state() { return g_state; }

bool startSync() {
  if (g_state == State::Joining || g_state == State::Syncing) return true;
  if (!g_ssid || !g_ssid[0] || strcmp(g_ssid, "your-ssid") == 0) return false;
  if (contentserver::radioSpent()) return false;  // its one session is spent
  if (contentserver::state() != contentserver::State::Off) {
    // The editor has the radio; tick() syncs over its connection.
    return contentserver::state() == contentserver::State::Running &&
           !contentserver::usingHotspot();
  }
  // The same trade the editor makes: the transport wants a large block of
  // internal RAM, and the SNES core holds one from boot.
  snes_ui::yieldWorkRamReserve();
  WiFi.mode(WIFI_STA);
  WiFi.begin(g_ssid, g_password);
  g_own_radio = true;
  g_state = State::Joining;
  g_state_ms = millis();
  Serial.printf("[time] joining \"%s\" for the time\n", g_ssid);
  return true;
}

void configure(const char *ssid, const char *password) {
  g_ssid = ssid;
  g_password = password;
}

bool canSync() {
  return g_ssid && g_ssid[0] && strcmp(g_ssid, "your-ssid") != 0 && !contentserver::radioSpent();
}

void tick(uint32_t now_ms) {
  switch (g_state) {
    case State::Joining:
      if (WiFi.status() == WL_CONNECTED) {
        startSntp();
      } else if (now_ms - g_state_ms > kJoinMs) {
        Serial.printf("[time] could not join (wifi status=%d)\n", (int)WiFi.status());
        endSession(State::Failed);
      }
      break;
    case State::Syncing:
      if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        recordSync();
        endSession(State::Done);
      } else if (now_ms - g_state_ms > kSyncMs) {
        Serial.println("[time] the network did not answer");
        endSession(State::Failed);
      }
      break;
    case State::Idle:
    case State::Done:
    case State::Failed:
      // The editor's connection is a chance to sync for free.
      if (!g_session_synced && contentserver::state() == contentserver::State::Running &&
          !contentserver::usingHotspot()) {
        g_own_radio = false;
        startSntp();
      }
      break;
  }
}

}  // namespace wallclock
}  // namespace tabulous
