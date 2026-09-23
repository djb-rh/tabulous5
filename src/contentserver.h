// Editing device content from a browser.
//
// Deliberately NOT a word-pack editor. It edits text files on LittleFS inside
// directories that have been registered as editable roots, so a future game
// registers its own directory and gets an editor with no changes here:
//
//   contentserver::addRoot("/trivia", "Trivia questions", ".txt");
//
// Everything game-specific therefore lives in the file format, not in this
// module or in the page it serves.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

class WebServer;

namespace tabulous {
namespace contentserver {

enum class State : uint8_t {
  Off,
  Connecting,
  Running,
  Failed,
};

// Two ways to reach the editor, because neither always works: joining the
// house network is convenient but needs credentials and a reachable router;
// the device's own hotspot works anywhere, including somewhere with no WiFi
// at all, at the cost of the phone leaving its normal network.
enum class Mode : uint8_t {
  Auto,         // try the network, fall back to the hotspot
  JoinNetwork,  // station only
  Hotspot,      // access point only
};

struct Root {
  std::string path;       // e.g. "/packs" — no trailing slash
  std::string label;      // shown in the browser
  std::string extension;  // ".txt"; new files get it appended
};

// Register an editable directory. Call before start(). Registering the same
// path twice replaces the previous entry.
void addRoot(const char *path, const char *label, const char *extension);
const std::vector<Root> &roots();

// Other modules add their own routes to the same server through this: it is
// called each time the server is (re)created, before it starts listening.
using RouteHook = void (*)(WebServer &server);
void addRouteHook(RouteHook hook);

// Brings up WiFi (via the ESP32-C6 co-processor) and starts the HTTP server.
// Non-blocking: poll state() and call loop() every tick.
void start(Mode mode, const char *ssid, const char *password);

// Restarts in the other mode, for when the chosen one isn't working.
void switchMode(Mode mode);

Mode mode();
bool usingHotspot();
// Whether the radio has already had its one session this boot. The
// co-processor's transport cannot be started twice - the second start
// asserts - so a second session means restarting the console first.
bool radioSpent();
// Called by whoever ends a radio session of their own (see wallclock).
void noteRadioSpent();
const char *hotspotSsid();
void stop();

void loop();
State state();

// Valid once state() is Running.
std::string url();
std::string ip();

// Bumped whenever a file is written or deleted, so the app can notice content
// has changed and reload it rather than polling the filesystem.
uint32_t revision();

const char *lastError();

}  // namespace contentserver
}  // namespace tabulous
