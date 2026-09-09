#include "emu_video.h"

#include <M5Unified.h>
#include <driver/ppa.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>

#include <cstring>

#include "theme.h"

namespace tabulous {
namespace emu_video {
namespace {

// The panel's own framebuffer, written directly.
//
// The panel is natively 720x1280 PORTRAIT and the UI runs 1280x720 landscape,
// so LovyanGFX's rotation maps a landscape scanline onto a COLUMN of the
// framebuffer — every pixel of a horizontal run lands a stride apart. That is
// what made pushImage cost 29 ms: not the copy, the stride.
uint8_t *g_fb = nullptr;
size_t g_fb_stride = 0;
uint8_t g_fb_rot = 1;

// The P4's Pixel Processing Accelerator scales, rotates and copies in
// hardware, which is the difference between 9.6 ms a frame and 1.5.
ppa_client_handle_t g_ppa = nullptr;
volatile bool g_ppa_busy = false;

uint16_t *g_buf[2] = {nullptr, nullptr};
int g_cur = 0;
int g_src_w = 0, g_src_h = 0, g_scale = 0;
size_t g_frame_bytes = 0;
Geometry g_geom;
uint32_t g_last_us = 0;

// From the PPA's interrupt: the transfer is done, its source buffer is free.
bool IRAM_ATTR onPpaDone(ppa_client_handle_t, ppa_event_data_t *, void *) {
  g_ppa_busy = false;
  return false;
}

// The hardware path. The framebuffer is described to the PPA in the panel's
// own portrait terms, and the picture is rotated into it: for rotation 1,
// logical x runs down the panel's rows and logical y backwards along its
// columns, which is a quarter turn clockwise — 270 counter-clockwise in the
// driver's vocabulary. Rotation 3 is the other way.
bool blitPpa(const uint16_t *src) {
  ppa_srm_oper_config_t op = {};
  op.in.buffer = src;
  op.in.pic_w = g_src_w;
  op.in.pic_h = g_src_h;
  op.in.block_w = g_src_w;
  op.in.block_h = g_src_h;
  op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  op.out.buffer = g_fb;
  op.out.buffer_size = g_fb_stride * theme::kW;
  op.out.pic_w = theme::kH;  // 720: the panel's own width
  op.out.pic_h = theme::kW;  // 1280
  op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  if (g_fb_rot == 1) {
    op.out.block_offset_x = theme::kH - g_geom.y - g_geom.h;
    op.out.block_offset_y = g_geom.x;
    op.rotation_angle = PPA_SRM_ROTATION_ANGLE_270;
  } else {
    op.out.block_offset_x = g_geom.y;
    op.out.block_offset_y = theme::kW - g_geom.x - g_geom.w;
    op.rotation_angle = PPA_SRM_ROTATION_ANGLE_90;
  }
  op.scale_x = (float)g_scale;
  op.scale_y = (float)g_scale;
  // Non-blocking when there is a second buffer to draw the next frame into;
  // the wait happens up front, for the transfer before this one.
  const bool overlap = g_buf[1] != nullptr;
  op.mode = overlap ? PPA_TRANS_MODE_NON_BLOCKING : PPA_TRANS_MODE_BLOCKING;
  waitIdle();
  // The source was just written by the CPU; make sure the DMA sees it.
  esp_cache_msync((void *)src, g_frame_bytes,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA |
                      ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  g_ppa_busy = overlap;
  if (ppa_do_scale_rotate_mirror(g_ppa, &op) != ESP_OK) {
    g_ppa_busy = false;
    return false;
  }
  return true;
}

// The fallback, for a machine whose scaler would not register. Walking DOWN a
// landscape column is what walks the framebuffer sequentially, so the scaling
// carries the transpose along for free; the strided access moves to the
// source, which is small and in internal RAM where stride costs almost
// nothing.
void blitCpu(const uint16_t *src) {
  for (int sx = 0; sx < g_src_w; sx++) {
    for (int k = 0; k < g_scale; k++) {
      const int lx = g_geom.x + sx * g_scale + k;
      uint16_t *dst;
      if (g_fb_rot == 1) {
        // row = lx, column = (kH - 1) - y: ascending address, descending y.
        dst = (uint16_t *)(g_fb + (size_t)lx * g_fb_stride) +
              (theme::kH - g_geom.y - g_geom.h);
      } else {
        dst = (uint16_t *)(g_fb + (size_t)(theme::kW - 1 - lx) * g_fb_stride) + g_geom.y;
      }
      for (int i = 0; i < g_src_h; i++) {
        const int sy = (g_fb_rot == 1) ? (g_src_h - 1 - i) : i;
        const uint16_t v = src[(size_t)sy * g_src_w + sx];
        for (int j = 0; j < g_scale; j++) *dst++ = v;
      }
    }
  }
  // The framebuffer is cached and the DSI scans it by DMA, so the writes have
  // to be pushed out or the panel shows stale pixels. One flush over the whole
  // touched span rather than hundreds of small ones.
  const size_t first_row =
      (g_fb_rot == 1) ? g_geom.x : (theme::kW - (g_geom.x + g_geom.w));
  esp_cache_msync(g_fb + first_row * g_fb_stride, (size_t)g_geom.w * g_fb_stride,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
}

}  // namespace

bool begin() {
  if (!g_fb) {
    // config_detail() is public, so this needs no games with protected members.
    auto *panel = (lgfx::Panel_DSI *)M5.Display.getPanel();
    if (!panel) return false;
    g_fb = (uint8_t *)panel->config_detail().buffer;
    g_fb_stride = ((size_t)panel->config().panel_width * 2 + 3) & ~(size_t)3;
  }
  g_fb_rot = (uint8_t)M5.Display.getRotation();
  if (!g_ppa) {
    ppa_client_config_t cfg = {};
    cfg.oper_type = PPA_OPERATION_SRM;
    cfg.max_pending_trans_num = 2;
    if (ppa_register_client(&cfg, &g_ppa) != ESP_OK) {
      g_ppa = nullptr;
      Serial.println("emu: PPA unavailable, CPU blit");
    } else {
      ppa_event_callbacks_t cbs = {};
      cbs.on_trans_done = onPpaDone;
      ppa_client_register_event_callbacks(g_ppa, &cbs);
    }
  }
  return g_fb != nullptr;
}

bool configure(int src_w, int src_h, int scale) {
  waitIdle();
  const size_t bytes = (size_t)src_w * src_h * 2;
  if (bytes != g_frame_bytes) release();
  g_src_w = src_w;
  g_src_h = src_h;
  g_scale = scale;
  g_frame_bytes = bytes;
  g_geom.w = src_w * scale;
  g_geom.h = src_h * scale;
  g_geom.x = (theme::kW - g_geom.w) / 2;
  g_geom.y = (theme::kH - g_geom.h) / 2;

  // Cache-line aligned: the scaler reads these by DMA. The first goes in
  // internal RAM, where the CPU fallback's strided reads are cheap; the
  // second takes PSRAM if internal has no room, which costs only the overlap.
  if (!g_buf[0]) {
    g_buf[0] = (uint16_t *)heap_caps_aligned_alloc(128, bytes,
                                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (!g_buf[1]) {
    g_buf[1] = (uint16_t *)heap_caps_aligned_alloc(128, bytes,
                                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!g_buf[1]) g_buf[1] = (uint16_t *)heap_caps_aligned_alloc(128, bytes, MALLOC_CAP_SPIRAM);
  }
  if (!g_buf[0]) return false;
  for (uint16_t *b : g_buf) {
    if (b) memset(b, 0, bytes);
  }
  g_cur = 0;
  return true;
}

void release() {
  waitIdle();
  for (uint16_t *&b : g_buf) {
    if (b) heap_caps_free(b);
    b = nullptr;
  }
  g_frame_bytes = 0;
  g_cur = 0;
}

uint16_t *frame() { return g_buf[g_cur]; }

void waitIdle() {
  // Bounded, so a wedged scaler cannot hang the console.
  const uint32_t t0 = micros();
  while (g_ppa_busy) {
    if (micros() - t0 > 40000) {
      g_ppa_busy = false;
      return;
    }
    taskYIELD();
  }
}

void present() {
  if (!g_fb || !g_buf[0]) return;
  const uint32_t t0 = micros();
  const uint16_t *src = g_buf[g_cur];
  if (g_ppa) {
    if (blitPpa(src)) {
      if (g_buf[1]) g_cur ^= 1;
      g_last_us = micros() - t0;
      return;
    }
    g_ppa = nullptr;  // once is enough; the CPU has it from here
  }
  blitCpu(src);
  g_last_us = micros() - t0;
}

Geometry geometry() { return g_geom; }
bool hardware() { return g_ppa != nullptr; }
uint32_t lastUs() { return g_last_us; }

}  // namespace emu_video
}  // namespace tabulous
