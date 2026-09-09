#include "rom_browser.h"

#include <LittleFS.h>
#include <M5Unified.h>
#include <dirent.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "audio.h"
#include "filemanager.h"
#include "sdcard.h"
#include "theme.h"
#include "uikit.h"

namespace tabulous {
namespace rom_browser {
namespace {

using namespace theme;
using uikit::Rect;
using uikit::gfx;

Config g_cfg;
rom_index::Index *g_lib = nullptr;
bool g_scanned = false;
uint32_t g_scanned_rev = 0;  // filemanager::revision() at the last scan
bool g_dirty = true;
const char *g_error = "";
uint8_t g_scale = 0;
bool g_portrait = false;

enum class Action : uint8_t {
  None, Pick, Back, PageUp, PageDown, Group, ToggleFav, Scale, Orient, Extra
};

// A page is fourteen rows, which is as many as read comfortably at this size;
// a group like S holds several hundred, so paging is by whole screens and the
// position is spelled out.
constexpr int kRows = 14;
constexpr int kRowH = 40, kRowGap = 2;
constexpr int kListTop = 100;
constexpr int kRailX = kMargin, kRailCellW = 60, kRailCols = 2;
constexpr int kListX = kMargin + kRailCols * (kRailCellW + 4) + 16;
int g_group = 2;  // 'A'
int g_page = 0;
// Whether the letter on the rail is the owner's choice or ours. Until they
// pick one, a system opens on its starred games: that is the shortlist, and on
// a card of thousands it is the only page anyone wants first.
bool g_group_chosen = false;

// Defined with the other list geometry, below; the scan needs it to decide
// which group to open on.
int groupTotal(int group);

void addAction(const Rect &r, Action a, int param = 0) {
  uikit::addTarget(r, (int)a, param);
}

// ---------------------------------------------------------------- scanning

void loadFavourites() {
  File f = LittleFS.open(g_cfg.favourites_file, "r");
  if (!f) {
    // Nobody has starred anything yet, so start from the shortlist if this
    // system has one. It is not written out until something is starred or
    // unstarred, so an empty file is never mistaken for a considered choice.
    if (g_cfg.default_favourites) g_lib->applyFavourites(g_cfg.default_favourites);
    return;
  }
  String text = f.readString();
  f.close();
  g_lib->applyFavourites(text.c_str());
}

void saveFavourites() {
  const std::string text = g_lib->favouritesText();
  File f = LittleFS.open(g_cfg.favourites_file, "w");
  if (!f) return;
  f.write((const uint8_t *)text.data(), text.size());
  f.close();
}

// Titles the web file manager has hidden: still on the card, kept out of the
// list. Dropped after the walk rather than filtered during it, because of what
// it costs to ask a filesystem whether a file exists.
//
// A FAT open that FINDS its file stops there — ~110 ms in a directory of
// 5,829. One that does not find it must compare every entry before it can say
// so, which measured at four seconds and made every boot's scan three times
// slower. The walk itself already passes .hidden if there is one, so the open
// only happens when there is something to open.
void dropHidden(rom_index::Index *lib, fs::FS &fs, const char *dir) {
  char path[300];
  snprintf(path, sizeof(path), "%s/.hidden", dir);
  File f = fs.open(path, "r");
  if (!f) return;
  const String text = f.readString();
  f.close();
  int at = 0;
  while (at < (int)text.length()) {
    int e = text.indexOf('\n', at);
    if (e < 0) e = text.length();
    String line = text.substring(at, e);
    line.trim();
    if (line.length()) lib->removeFile(line.c_str());
    at = e + 1;
  }
}

bool isDotHidden(const char *name) { return strcmp(name, ".hidden") == 0; }

// The card is walked with readdir rather than the Arduino File API: that API
// opens every entry it lists, and one open in a FAT directory of thousands of
// files is a linear search through the whole directory. Names are all the
// index needs. One level of subdirectories is included, so a card laid out in
// folders works the same as a flat one.
void scanCardDir(const char *vfs_dir, const char *lib_dir, bool recurse) {
  DIR *d = opendir(vfs_dir);
  if (!d) return;
  bool has_hidden = false;
  for (struct dirent *e = readdir(d); e; e = readdir(d)) {
    if (e->d_name[0] == '.') {
      has_hidden = has_hidden || isDotHidden(e->d_name);
      continue;
    }
    if (e->d_type == DT_DIR) {
      if (!recurse) continue;
      char sub_vfs[300], sub_lib[160];
      snprintf(sub_vfs, sizeof(sub_vfs), "%s/%s", vfs_dir, e->d_name);
      snprintf(sub_lib, sizeof(sub_lib), "%s/%s", lib_dir, e->d_name);
      scanCardDir(sub_vfs, sub_lib, false);
      continue;
    }
    g_lib->add(lib_dir, e->d_name, rom_index::kCard);
  }
  closedir(d);
  if (has_hidden) dropHidden(g_lib, sdcard::fs(), lib_dir);
}

void scan() {
  const uint32_t t0 = millis();
  g_lib->clear();

  File dir = LittleFS.open(g_cfg.dir);
  if (dir && dir.isDirectory()) {
    bool has_hidden = false;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      if (f.isDirectory()) continue;
      if (f.name()[0] == '.') {
        has_hidden = has_hidden || isDotHidden(f.name());
        continue;
      }
      g_lib->add(g_cfg.dir, f.name(), rom_index::kFlash);
    }
    if (has_hidden) dropHidden(g_lib, LittleFS, g_cfg.dir);
  }
  const int flash_n = g_lib->size();

  if (sdcard::begin()) {
    char vfs[64];
    snprintf(vfs, sizeof(vfs), "%s%s", sdcard::mountPoint(), g_cfg.dir);
    scanCardDir(vfs, g_cfg.dir, true);
  }
  g_lib->finish();
  loadFavourites();
  // Open on the starred games, unless a letter has been picked since this
  // system was opened, or nothing is starred to show.
  if (!g_group_chosen) {
    g_group = groupTotal(rom_index::kFavourites) > 0 ? rom_index::kFavourites : 2;
    g_page = 0;
  }
  g_scanned = true;
  g_scanned_rev = filemanager::revision();
  Serial.printf("%s: %d ROMs (%d built in, %d on card) in %lu ms\n", g_cfg.title,
                g_lib->size(), flash_n, g_lib->size() - flash_n,
                (unsigned long)(millis() - t0));
}

// ----------------------------------------------------------------- drawing

// A five-point star, as a fan of triangles from the centre. Fonts here are
// ASCII, so the glyph is drawn rather than typed.
void drawStar(int cx, int cy, int r, uint16_t colour, bool filled) {
  auto &g = gfx();
  int px[10], py[10];
  for (int k = 0; k < 10; k++) {
    const float a = -1.5707963f + k * 0.62831853f;  // start at the top
    const float rr = (k & 1) ? r * 0.42f : (float)r;
    px[k] = cx + (int)lroundf(cosf(a) * rr);
    py[k] = cy + (int)lroundf(sinf(a) * rr);
  }
  if (filled) {
    for (int k = 0; k < 10; k++) {
      g.fillTriangle(cx, cy, px[k], py[k], px[(k + 1) % 10], py[(k + 1) % 10], colour);
    }
  } else {
    for (int k = 0; k < 10; k++) {
      g.drawLine(px[k], py[k], px[(k + 1) % 10], py[(k + 1) % 10], colour);
    }
  }
}

int groupTotal(int group) {
  return group == rom_index::kFavourites ? g_lib->favouriteCount()
                                         : g_lib->groupCount(group);
}

int groupItem(int group, int n) {
  return group == rom_index::kFavourites ? g_lib->favouriteAt(n)
                                         : g_lib->groupBegin(group) + n;
}

void drawRail() {
  const int rows = rom_index::kGroups / kRailCols;
  for (int gi = 0; gi < rom_index::kGroups; gi++) {
    const int col = gi / rows, row = gi % rows;
    const Rect cell{kRailX + col * (kRailCellW + 4), kListTop + row * (kRowH + kRowGap),
                    kRailCellW, kRowH};
    const bool current = gi == g_group;
    const bool any = groupTotal(gi) > 0;
    uikit::fillRoundRectFast(cell.x, cell.y, cell.w, cell.h, 8,
                             current ? kAccent : any ? kSurfaceLift : kSurface);
    const uint16_t ink = current ? inkFor(theme::isLight() ? 0xA96A00u : 0xFFC53Du)
                                 : any ? kText : kMuted;
    if (gi == rom_index::kFavourites) {
      drawStar(cell.x + cell.w / 2, cell.y + cell.h / 2, 13, ink, true);
    } else {
      uikit::drawLabel(rom_index::groupLabel(gi), cell.x + cell.w / 2,
                       cell.y + cell.h / 2 + 1, ink, &fonts::FreeSansBold12pt7b,
                       middle_center);
    }
    addAction(cell, Action::Group, gi);
  }
}

}  // namespace

void begin(const Config &config, uint8_t scale, bool portrait) {
  // One browser serves every system, so opening a different one has to throw
  // the last one's library away. Keeping it meant the first system opened
  // after a boot owned the screen: pick NES, then Arcade, and Arcade showed
  // the NES cartridges -- an index is built for one file extension and the
  // scan is remembered by directory.
  const bool switched = g_lib == nullptr || g_cfg.dir == nullptr ||
                        strcmp(g_cfg.dir, config.dir) != 0 ||
                        strcasecmp(g_cfg.extension, config.extension) != 0;
  g_portrait = portrait;
  g_cfg = config;
  g_scale = scale == config.scale_value[1] ? config.scale_value[1] : config.scale_value[0];
  if (switched) {
    delete g_lib;
    g_lib = new rom_index::Index(config.extension);
    g_scanned = false;
    // Page eleven of the NES list is nowhere in a list of seventy, and the
    // next system opens on its own starred games rather than this one's letter.
    g_group = 2;  // 'A'
    g_page = 0;
    g_group_chosen = false;
  }
  g_error = "";
  g_dirty = true;
}

void rescan() { g_scanned = false; }

void ensureScanned() {
  if (g_scanned && filemanager::revision() != g_scanned_rev) g_scanned = false;
  if (g_scanned) return;
  // A card of thousands takes a moment to walk; say so rather than sit on the
  // previous screen.
  showBusy("Reading the game list...");
  scan();
  g_dirty = true;
}

bool dirty() { return g_dirty; }
void invalidate() { g_dirty = true; }
int size() { return g_lib ? g_lib->size() : 0; }
const rom_index::Item &item(int i) { return g_lib->at(i); }
uint8_t scale() { return g_scale; }
bool portrait() { return g_portrait; }
void setExtraLabel(const char *label) {
  g_cfg.extra_label = label;
  g_dirty = true;
}

void showBusy(const char *message) {
  auto &g = gfx();
  g.fillScreen(kBg);
  uikit::drawLabel(message, kW / 2, kH / 2, kMuted, &fonts::FreeSansBold18pt7b,
                   middle_center);
  uikit::present();
  // The list is gone from the screen, so its hit targets have to go with it:
  // a tap during the wait must not land on a row that is no longer there.
  uikit::clearTargets();
  g_dirty = true;
}

void setError(const char *message) {
  g_error = message ? message : "";
  g_dirty = true;
}

fs::FS &fsFor(const rom_index::Item &it) {
  return it.source == rom_index::kCard ? sdcard::fs() : (fs::FS &)LittleFS;
}

void probeItem(int i) {
  rom_index::Item &it = g_lib->mutableAt(i);
  if (it.status >= 0 || !g_cfg.probe) return;
  g_cfg.probe(fsFor(it), &it);
}

void draw() {
  g_dirty = false;
  auto &g = gfx();
  g.fillScreen(kBg);
  uikit::drawLabel(g_cfg.title, kMargin, 46, kText, &fonts::FreeSansBold24pt7b,
                   middle_left);


  const Rect back{kW - kMargin - 190, 18, 190, 62};
  uikit::drawButton(back, "MENU", kSurfaceLift, kText, &fonts::FreeSansBold12pt7b);
  addAction(back, Action::Back);

  // Picture size, as a toggle in the header: it is the one thing about play
  // that is decided before a game starts.
  const Rect size_btn{back.x - 12 - 100 - 12 - 100 - 12 - 200, 18, 200, 62};
  const bool second = g_scale == g_cfg.scale_value[1];
  uikit::drawButton(size_btn, g_cfg.scale_label[second ? 1 : 0], kSurfaceLift, kText,
                    &fonts::FreeSansBold12pt7b);
  addAction(size_btn, Action::Scale);

  // Which way the picture goes. Upright is how the cabinet's monitor stood;
  // sideways lays it down and fills more of this screen.
  int next_x = size_btn.x;
  if (g_cfg.orientable) {
    const Rect orient{next_x - 12 - 170, 18, 170, 62};
    uikit::drawButton(orient, g_portrait ? "UPRIGHT" : "SIDEWAYS", kSurfaceLift, kText,
                      &fonts::FreeSansBold12pt7b);
    addAction(orient, Action::Orient);
    next_x = orient.x;
  }
  if (g_cfg.extra_label) {
    const Rect extra{next_x - 12 - 150, 18, 150, 62};
    uikit::drawButton(extra, g_cfg.extra_label, kSurfaceLift, kText,
                      &fonts::FreeSansBold12pt7b);
    addAction(extra, Action::Extra);
  }

  if (g_lib->size() == 0) {
    uikit::drawLabel("No ROMs found", kW / 2, 300, kMuted, &fonts::FreeSansBold18pt7b,
                     middle_center);
    uikit::drawLabel(g_cfg.empty_hint, kW / 2, 350, kMuted, &fonts::FreeSans12pt7b,
                     middle_center);
    return;
  }

  drawRail();

  const int total = groupTotal(g_group);
  const int pages = total > 0 ? (total + kRows - 1) / kRows : 1;
  if (g_page >= pages) g_page = pages - 1;
  if (g_page < 0) g_page = 0;

  // Where we are, spelled out next to the title: group, how many, which page.
  char where[96];
  if (g_group == rom_index::kFavourites) {
    snprintf(where, sizeof(where), "Favourites  %d", total);
  } else {
    snprintf(where, sizeof(where), "%s  %d of %d", rom_index::groupLabel(g_group), total,
             g_lib->size());
  }
  // Under the name rather than beside it. Beside it, a fixed offset that
  // suited "NES" had "GAME BOY" written through it, and measuring the name
  // only moved the collision along to the buttons. There is a whole row here.
  uikit::drawLabel(g_error[0] ? g_error : where, kMargin, 82,
                   g_error[0] ? kDanger : kMuted, &fonts::FreeSans12pt7b, middle_left);

  const int list_w = kW - kMargin - kListX;
  if (total == 0) {
    uikit::drawLabel(g_group == rom_index::kFavourites
                         ? "Tap the star on a game to keep it here"
                         : "Nothing filed here",
                     kListX + list_w / 2, kListTop + 200, kMuted,
                     &fonts::FreeSansBold18pt7b, middle_center);
    return;
  }

  for (int slot = 0; slot < kRows; slot++) {
    const int n = g_page * kRows + slot;
    if (n >= total) break;
    const int i = groupItem(g_group, n);
    const rom_index::Item &e = g_lib->at(i);
    const Rect row{kListX, kListTop + slot * (kRowH + kRowGap), list_w, kRowH};
    // A title already found unplayable stays listed, dimmed, with the reason:
    // hiding it would leave the owner hunting for a file that is right there.
    const bool bad = e.status == 1;
    uikit::fillRoundRectFast(row.x, row.y, row.w, row.h, 8, bad ? kSurface : kSurfaceLift);
    const uint16_t ink = bad ? kMuted : kText;
    uikit::drawLabel(e.name, row.x + 16, row.y + kRowH / 2 + 1, ink,
                     &fonts::FreeSansBold12pt7b, middle_left);
    if (bad) {
      uikit::drawLabel(e.problem, row.x + row.w - 200, row.y + kRowH / 2 + 1, kDanger,
                       &fonts::FreeSans9pt7b, middle_right);
    } else if (e.source == rom_index::kFlash) {
      uikit::drawLabel("built in", row.x + row.w - 200, row.y + kRowH / 2 + 1, kMuted,
                       &fonts::FreeSans9pt7b, middle_right);
    }
    const Rect star{row.x + row.w - 64, row.y, 64, kRowH};
    drawStar(star.x + star.w / 2, star.y + star.h / 2, 13, e.fav ? kAccent : kMuted, e.fav);
    const Rect title{row.x, row.y, row.w - 64, row.h};
    if (!bad) addAction(title, Action::Pick, i);
    addAction(star, Action::ToggleFav, i);
  }

  if (pages > 1) {
    const Rect up{back.x - 12 - 100 - 12 - 100, 18, 100, 62};
    const Rect down{back.x - 12 - 100, 18, 100, 62};
    const bool can_up = g_page > 0;
    const bool can_down = g_page + 1 < pages;
    uikit::drawArrowButton(up, true, can_up ? kSurfaceLift : kSurface, can_up ? kText : kMuted);
    uikit::drawArrowButton(down, false, can_down ? kSurfaceLift : kSurface,
                           can_down ? kText : kMuted);
    if (can_up) addAction(up, Action::PageUp);
    if (can_down) addAction(down, Action::PageDown);
    char pos[48];
    snprintf(pos, sizeof(pos), "%d / %d", g_page + 1, pages);
    uikit::drawLabel(pos, up.x + 106, 92, kMuted, &fonts::FreeSansBold12pt7b, middle_center);
  }
}

Result handleTap(int x, int y, int *index) {
  int action = 0, param = 0;
  if (!uikit::findTarget(x, y, &action, &param)) return Result::None;
  switch ((Action)action) {
    case Action::Pick:
      g_error = "";
      if (index) *index = param;
      return Result::Launch;
    case Action::Group:
      audio::select();
      if (param != g_group) g_page = 0;
      g_group = param;
      g_group_chosen = true;
      g_dirty = true;
      break;
    case Action::ToggleFav:
      audio::select();
      g_lib->setFavourite(param, !g_lib->at(param).fav);
      saveFavourites();
      g_dirty = true;
      break;
    case Action::Extra:
      audio::select();
      return Result::Extra;
    case Action::Orient:
      audio::select();
      g_portrait = !g_portrait;
      g_dirty = true;
      return Result::OrientationChanged;
    case Action::Scale:
      audio::select();
      g_scale = (g_scale == g_cfg.scale_value[1]) ? g_cfg.scale_value[0]
                                                  : g_cfg.scale_value[1];
      g_dirty = true;
      return Result::ScaleChanged;
    case Action::PageUp:
      audio::select();
      g_page--;
      g_dirty = true;
      break;
    case Action::PageDown:
      audio::select();
      g_page++;
      g_dirty = true;
      break;
    case Action::Back:
      return Result::Back;
    case Action::None:
      break;
  }
  return Result::None;
}

}  // namespace rom_browser
}  // namespace tabulous
