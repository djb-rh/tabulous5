#include "settings_store.h"

#include <Preferences.h>

namespace tabulous {
namespace settings_store {
namespace {

// Deliberately still "phrasecraze": this is a storage key, not a brand. Renaming
// it would orphan the settings and orientation calibration already on the device.
constexpr const char *kNamespace = "phrasecraze";

// Bumped if the meaning of a stored field changes, so an old value can never
// be read back as something it isn't.
constexpr uint8_t kVersion = 1;

Preferences g_prefs;

// What is currently believed to be on flash, so save() can skip unchanged
// fields — and skip opening NVS at all when nothing changed.
Settings g_snapshot;
bool g_have_snapshot = false;

bool differs(const Settings &a, const Settings &b) {
  return a.timer.min_ms != b.timer.min_ms || a.timer.max_ms != b.timer.max_ms ||
         a.target_score != b.target_score ||
         a.skips_per_round != b.skips_per_round || a.volume != b.volume ||
         a.sound_enabled != b.sound_enabled || a.auto_rotate != b.auto_rotate ||
         a.fast_charge != b.fast_charge || a.usb_power != b.usb_power ||
         a.usb_data_auto != b.usb_data_auto ||
         a.flip_delay_ms != b.flip_delay_ms ||
         a.max_difficulty != b.max_difficulty || a.scoring != b.scoring ||
         a.team_names[0] != b.team_names[0] ||
         a.team_names[1] != b.team_names[1] ||
         a.enabled_packs != b.enabled_packs;
}

}  // namespace

void load(Settings *out) {
  if (!out) return;
  if (!g_prefs.begin(kNamespace, true)) return;

  if (g_prefs.getUChar("s_ver", 0) != kVersion) {
    g_prefs.end();
    return;  // absent or stale — keep the compiled-in defaults
  }

  const Settings d;  // defaults, used as the fallback for each field
  out->timer.min_ms = g_prefs.getUInt("s_tmin", d.timer.min_ms);
  out->timer.max_ms = g_prefs.getUInt("s_tmax", d.timer.max_ms);
  out->target_score = g_prefs.getUChar("s_target", d.target_score);
  out->skips_per_round = g_prefs.getUChar("s_skips", d.skips_per_round);
  out->volume = g_prefs.getUChar("s_vol", d.volume);
  out->sound_enabled = g_prefs.getBool("s_snd", d.sound_enabled);
  out->auto_rotate = g_prefs.getBool("s_rot", d.auto_rotate);
  out->fast_charge = g_prefs.getBool("s_qc", d.fast_charge);
  out->usb_power = g_prefs.getBool("s_usb5v", d.usb_power);
  out->usb_data_auto = g_prefs.getBool("s_usbdat", d.usb_data_auto);
  out->flip_delay_ms = g_prefs.getUShort("s_flipms", d.flip_delay_ms);
  out->max_difficulty =
      (Difficulty)g_prefs.getUChar("s_diff", (uint8_t)d.max_difficulty);
  out->scoring = (ScoringMode)g_prefs.getUChar("s_score", (uint8_t)d.scoring);
  out->enabled_packs = g_prefs.getUInt("s_packs", d.enabled_packs);

  String a = g_prefs.getString("s_team0", d.team_names[0].c_str());
  String b = g_prefs.getString("s_team1", d.team_names[1].c_str());
  if (a.length()) out->team_names[0] = std::string(a.c_str());
  if (b.length()) out->team_names[1] = std::string(b.c_str());

  // Guard against a stored range that would make pickRoundDuration nonsense.
  if (out->timer.max_ms < out->timer.min_ms) {
    out->timer.max_ms = out->timer.min_ms;
  }

  g_prefs.end();

  g_snapshot = *out;
  g_have_snapshot = true;
}

void save(const Settings &s) {
  // NVS writes are flash writes: slow, and they block the loop that reads
  // touch. Writing all thirteen keys on every exit from the settings screen
  // was costing hundreds of milliseconds for, usually, no change at all. So
  // only fields that actually differ are written.
  if (g_have_snapshot && !differs(s, g_snapshot)) return;

  if (!g_prefs.begin(kNamespace, false)) return;

  const bool all = !g_have_snapshot;
  const Settings &o = g_snapshot;

  if (all) g_prefs.putUChar("s_ver", kVersion);
  if (all || s.timer.min_ms != o.timer.min_ms)
    g_prefs.putUInt("s_tmin", s.timer.min_ms);
  if (all || s.timer.max_ms != o.timer.max_ms)
    g_prefs.putUInt("s_tmax", s.timer.max_ms);
  if (all || s.target_score != o.target_score)
    g_prefs.putUChar("s_target", s.target_score);
  if (all || s.skips_per_round != o.skips_per_round)
    g_prefs.putUChar("s_skips", s.skips_per_round);
  if (all || s.volume != o.volume) g_prefs.putUChar("s_vol", s.volume);
  if (all || s.sound_enabled != o.sound_enabled)
    g_prefs.putBool("s_snd", s.sound_enabled);
  if (all || s.auto_rotate != o.auto_rotate)
    g_prefs.putBool("s_rot", s.auto_rotate);
  if (all || s.fast_charge != o.fast_charge)
    g_prefs.putBool("s_qc", s.fast_charge);
  if (all || s.usb_power != o.usb_power)
    g_prefs.putBool("s_usb5v", s.usb_power);
  if (all || s.usb_data_auto != o.usb_data_auto)
    g_prefs.putBool("s_usbdat", s.usb_data_auto);
  if (all || s.flip_delay_ms != o.flip_delay_ms)
    g_prefs.putUShort("s_flipms", s.flip_delay_ms);
  if (all || s.max_difficulty != o.max_difficulty)
    g_prefs.putUChar("s_diff", (uint8_t)s.max_difficulty);
  if (all || s.scoring != o.scoring)
    g_prefs.putUChar("s_score", (uint8_t)s.scoring);
  if (all || s.enabled_packs != o.enabled_packs)
    g_prefs.putUInt("s_packs", s.enabled_packs);
  if (all || s.team_names[0] != o.team_names[0])
    g_prefs.putString("s_team0", s.team_names[0].c_str());
  if (all || s.team_names[1] != o.team_names[1])
    g_prefs.putString("s_team1", s.team_names[1].c_str());

  g_prefs.end();

  g_snapshot = s;
  g_have_snapshot = true;
}

namespace {

fivehead::Settings g_five_snapshot;
bool g_have_five = false;

bool fiveDiffers(const fivehead::Settings &a, const fivehead::Settings &b) {
  return a.round_ms != b.round_ms || a.ready_ms != b.ready_ms ||
         a.rounds_each != b.rounds_each ||
         a.max_difficulty != b.max_difficulty ||
         a.tilt_swapped != b.tilt_swapped ||
         a.tilt_sensitivity != b.tilt_sensitivity ||
         a.flash_ms != b.flash_ms || a.team_names[0] != b.team_names[0] ||
         a.team_names[1] != b.team_names[1] ||
         a.enabled_packs != b.enabled_packs;
}

}  // namespace

void loadFive(fivehead::Settings *out) {
  if (!out) return;
  if (!g_prefs.begin(kNamespace, true)) return;
  if (g_prefs.getUChar("f_ver", 0) != kVersion) {
    g_prefs.end();
    return;
  }
  const fivehead::Settings d;
  out->round_ms = g_prefs.getUInt("f_round", d.round_ms);
  out->ready_ms = g_prefs.getUInt("f_ready", d.ready_ms);
  out->rounds_each = g_prefs.getUChar("f_turns", d.rounds_each);
  out->flash_ms = g_prefs.getUShort("f_flash", d.flash_ms);
  out->tilt_swapped = g_prefs.getBool("f_swap", d.tilt_swapped);
  out->max_difficulty =
      (Difficulty)g_prefs.getUChar("f_diff", (uint8_t)d.max_difficulty);
  out->tilt_sensitivity = (fivehead::TiltSensitivity)g_prefs.getUChar(
      "f_sens", (uint8_t)d.tilt_sensitivity);
  String fa = g_prefs.getString("f_team0", d.team_names[0].c_str());
  String fb = g_prefs.getString("f_team1", d.team_names[1].c_str());
  if (fa.length()) out->team_names[0] = std::string(fa.c_str());
  if (fb.length()) out->team_names[1] = std::string(fb.c_str());
  out->enabled_packs = g_prefs.getUInt("f_packs", d.enabled_packs);
  g_prefs.end();
  g_five_snapshot = *out;
  g_have_five = true;
}

void saveFive(const fivehead::Settings &s) {
  if (g_have_five && !fiveDiffers(s, g_five_snapshot)) return;
  if (!g_prefs.begin(kNamespace, false)) return;
  g_prefs.putUChar("f_ver", kVersion);
  g_prefs.putUInt("f_round", s.round_ms);
  g_prefs.putUInt("f_ready", s.ready_ms);
  g_prefs.putUChar("f_turns", s.rounds_each);
  g_prefs.putUShort("f_flash", s.flash_ms);
  g_prefs.putBool("f_swap", s.tilt_swapped);
  g_prefs.putUChar("f_diff", (uint8_t)s.max_difficulty);
  g_prefs.putUChar("f_sens", (uint8_t)s.tilt_sensitivity);
  g_prefs.putString("f_team0", s.team_names[0].c_str());
  g_prefs.putString("f_team1", s.team_names[1].c_str());
  g_prefs.putUInt("f_packs", s.enabled_packs);
  g_prefs.end();
  g_five_snapshot = s;
  g_have_five = true;
}

namespace {

SolitaireSettings g_sol_snapshot;
bool g_have_sol = false;

bool solDiffers(const SolitaireSettings &a, const SolitaireSettings &b) {
  return a.draw_three != b.draw_three || a.max_passes != b.max_passes ||
         a.tap_to_foundation != b.tap_to_foundation ||
         a.show_timer != b.show_timer;
}

}  // namespace

void loadMenu(MenuPrefs *out, uint8_t entry_count) {
  if (entry_count > kMaxMenuEntries) entry_count = kMaxMenuEntries;

  uint8_t stored[kMaxMenuEntries] = {0};
  size_t stored_len = 0;
  uint16_t hidden = 0;
  if (g_prefs.begin(kNamespace, true)) {
    stored_len = g_prefs.getBytes("menu_order", stored, sizeof(stored));
    hidden = (uint16_t)g_prefs.getUShort("menu_hidden", 0);
    g_prefs.end();
  }

  bool seen[kMaxMenuEntries] = {false};
  out->count = 0;
  for (size_t i = 0; i < stored_len && out->count < entry_count; i++) {
    const uint8_t idx = stored[i];
    if (idx >= entry_count || seen[idx]) continue;  // stale or duplicated
    seen[idx] = true;
    out->order[out->count++] = idx;
  }
  // Anything the stored order didn't mention — a game added since it was
  // written — goes on the end, visible.
  for (uint8_t i = 0; i < entry_count; i++) {
    if (!seen[i]) out->order[out->count++] = i;
  }

  // Never let a stale mask hide everything: a launcher with no games in it
  // offers no way back to this screen to undo it.
  out->hidden = hidden;
  bool any = false;
  for (uint8_t i = 0; i < out->count; i++) {
    if (!(out->hidden & (1u << out->order[i]))) any = true;
  }
  if (!any) out->hidden = 0;
}

void saveMenu(const MenuPrefs &prefs) {
  if (!g_prefs.begin(kNamespace, false)) return;
  g_prefs.putBytes("menu_order", prefs.order, prefs.count);
  g_prefs.putUShort("menu_hidden", prefs.hidden);
  g_prefs.end();
}

bool loadLightTheme() {
  if (!g_prefs.begin(kNamespace, true)) return false;
  const bool light = g_prefs.getBool("ui_light", false);
  g_prefs.end();
  return light;
}

void saveLightTheme(bool light) {
  if (!g_prefs.begin(kNamespace, false)) return;
  g_prefs.putBool("ui_light", light);
  g_prefs.end();
}

void loadNes(NesSettings *out) {
  if (!out) return;
  if (!g_prefs.begin(kNamespace, true)) return;
  const uint8_t scale = g_prefs.getUChar("nes_scale", out->scale);
  g_prefs.end();
  if (scale == 2 || scale == 3) out->scale = scale;
}

void saveNes(const NesSettings &settings) {
  if (!g_prefs.begin(kNamespace, false)) return;
  g_prefs.putUChar("nes_scale", settings.scale);
  g_prefs.end();
}

void loadGb(GbSettings *out) {
  if (!out) return;
  if (!g_prefs.begin(kNamespace, true)) return;
  const uint8_t scale = g_prefs.getUChar("gb_scale", out->scale);
  out->palette = g_prefs.getUChar("gb_palette", out->palette);
  g_prefs.end();
  if (scale == 3 || scale == 5) out->scale = scale;
}

void saveGb(const GbSettings &settings) {
  if (!g_prefs.begin(kNamespace, false)) return;
  g_prefs.putUChar("gb_scale", settings.scale);
  g_prefs.putUChar("gb_palette", settings.palette);
  g_prefs.end();
}

void loadSnes(SnesSettings *out) {
  if (!out) return;
  if (!g_prefs.begin(kNamespace, true)) return;
  const uint8_t scale = g_prefs.getUChar("snes_scale", out->scale);
  g_prefs.end();
  if (scale == 2 || scale == 3) out->scale = scale;
}

void saveSnes(const SnesSettings &settings) {
  if (!g_prefs.begin(kNamespace, false)) return;
  g_prefs.putUChar("snes_scale", settings.scale);
  g_prefs.end();
}

void loadArcade(ArcadeSettings *out) {
  if (!out) return;
  if (!g_prefs.begin(kNamespace, true)) return;
  const uint8_t scale = g_prefs.getUChar("arc_scale", out->scale);
  out->portrait = g_prefs.getBool("arc_portrait", out->portrait);
  out->dsw1 = g_prefs.getUChar("arc_dsw1", out->dsw1);
  g_prefs.end();
  if (scale == 2 || scale == 5) out->scale = scale;
}

void saveArcade(const ArcadeSettings &settings) {
  if (!g_prefs.begin(kNamespace, false)) return;
  g_prefs.putUChar("arc_scale", settings.scale);
  g_prefs.putBool("arc_portrait", settings.portrait);
  g_prefs.putUChar("arc_dsw1", settings.dsw1);
  g_prefs.end();
}

void loadPadMap(padmap::Map *out) {
  if (!out) return;
  if (!g_prefs.begin(kNamespace, true)) return;
  if (g_prefs.isKey("pad_a")) {
    out->a = g_prefs.getUChar("pad_a", out->a);
    out->a2 = g_prefs.getUChar("pad_a2", out->a2);
    out->b = g_prefs.getUChar("pad_b", out->b);
    out->b2 = g_prefs.getUChar("pad_b2", out->b2);
    out->select = g_prefs.getUChar("pad_sel", out->select);
    out->start = g_prefs.getUChar("pad_start", out->start);
  }
  g_prefs.end();
}

void savePadMap(const padmap::Map &map) {
  if (!g_prefs.begin(kNamespace, false)) return;
  g_prefs.putUChar("pad_a", map.a);
  g_prefs.putUChar("pad_a2", map.a2);
  g_prefs.putUChar("pad_b", map.b);
  g_prefs.putUChar("pad_b2", map.b2);
  g_prefs.putUChar("pad_sel", map.select);
  g_prefs.putUChar("pad_start", map.start);
  g_prefs.end();
}

void loadSolitaire(SolitaireSettings *out) {
  if (!out) return;
  if (!g_prefs.begin(kNamespace, true)) return;
  if (g_prefs.getUChar("l_ver", 0) != kVersion) {
    g_prefs.end();
    return;
  }
  const SolitaireSettings d;
  out->draw_three = g_prefs.getBool("l_draw3", d.draw_three);
  out->max_passes = g_prefs.getChar("l_passes", (int8_t)d.max_passes);
  out->tap_to_foundation = g_prefs.getBool("l_tapf", d.tap_to_foundation);
  out->show_timer = g_prefs.getBool("l_timer", d.show_timer);
  g_prefs.end();
  g_sol_snapshot = *out;
  g_have_sol = true;
}

void saveSolitaire(const SolitaireSettings &s) {
  if (g_have_sol && !solDiffers(s, g_sol_snapshot)) return;
  if (!g_prefs.begin(kNamespace, false)) return;
  g_prefs.putUChar("l_ver", kVersion);
  g_prefs.putBool("l_draw3", s.draw_three);
  g_prefs.putChar("l_passes", (int8_t)s.max_passes);
  g_prefs.putBool("l_tapf", s.tap_to_foundation);
  g_prefs.putBool("l_timer", s.show_timer);
  g_prefs.end();
  g_sol_snapshot = s;
  g_have_sol = true;
}

}  // namespace settings_store
}  // namespace tabulous
