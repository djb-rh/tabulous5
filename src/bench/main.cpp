// Display throughput benchmark — how fast can we get emulator frames onto the
// Tab5's panel?
//
// This exists because the only number we had was ~20.5 Mpx/s, measured from
// rounded rects and text through the drawing API. That is not representative
// of an emulator, which prepares a framebuffer and blits it in one call. The
// question this answers: is a 60 fps NES (768x720 at 3x) or Game Boy (800x720
// at 5x) actually achievable, or does the scale factor have to come down?
//
// Build and run with:  pio run -e bench -t upload && pio device monitor

#include <M5Unified.h>
#include <esp_heap_caps.h>

namespace {

struct Case {
  const char *name;
  int w, h;
};

// Every one of these is an integer scale that fits the 1280x720 panel.
const Case kCases[] = {
    {"NES 3x  768x720", 768, 720},
    {"NES 2x  512x480", 512, 480},
    {"GB  5x  800x720", 800, 720},
    {"GB  4x  640x576", 640, 576},
    {"full   1280x720", 1280, 720},
};

// A width sweep around 768. The first run showed 768, 512 and 1280 running ~10x
// slower than 800 and 640 through identical code, and those are exactly the
// widths whose row stride (w * 2 bytes) is a multiple of 512 — the signature of
// source and destination rows colliding in the same cache sets. If that is what
// this is, every 512-byte-stride width here is slow and its neighbours are fast,
// and the fix for an emulator is to pad the framebuffer stride.
const Case kSweep[] = {
    {"760x720 (1520 B)", 760, 720},
    {"764x720 (1528 B)", 764, 720},
    {"768x720 (1536 B)", 768, 720},
    {"772x720 (1544 B)", 772, 720},
    {"776x720 (1552 B)", 776, 720},
};

constexpr int kFrames = 30;

uint16_t *alloc(size_t px, uint32_t caps) {
  return (uint16_t *)heap_caps_malloc(px * sizeof(uint16_t), caps);
}

void fillPattern(uint16_t *buf, int w, int h, int phase) {
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      buf[y * w + x] = (uint16_t)((x + y + phase) * 37);
    }
  }
}

void report(const char *label, int w, int h, uint32_t total_us, int frames) {
  const double per = (double)total_us / frames;
  const double mpx = (double)w * h * frames / total_us;  // px/us == Mpx/s
  const double mbs = mpx * 2.0;                          // 16-bit pixels
  Serial.printf("%-22s %7.2f ms/frame  %6.1f fps  %5.1f Mpx/s  %5.1f MB/s\n",
                label, per / 1000.0, 1000000.0 / per, mpx, mbs);
}

// Blit a prepared buffer, waiting for DMA so the timing is honest.
uint32_t timePush(const uint16_t *buf, int w, int h) {
  const int x = (M5.Display.width() - w) / 2;
  const int y = (M5.Display.height() - h) / 2;
  const uint32_t t0 = micros();
  for (int i = 0; i < kFrames; i++) {
    M5.Display.startWrite();
    M5.Display.pushImage(x, y, w, h, buf);
    M5.Display.endWrite();
    M5.Display.waitDMA();
  }
  return micros() - t0;
}

// Nearest-neighbour upscale, the cost an emulator pays before it can blit.
uint32_t timeScale(const uint16_t *src, int sw, int sh, uint16_t *dst,
                   int scale) {
  const int dw = sw * scale;
  const uint32_t t0 = micros();
  for (int f = 0; f < kFrames; f++) {
    for (int sy = 0; sy < sh; sy++) {
      const uint16_t *srow = src + sy * sw;
      for (int r = 0; r < scale; r++) {
        uint16_t *drow = dst + (size_t)(sy * scale + r) * dw;
        for (int sx = 0; sx < sw; sx++) {
          const uint16_t v = srow[sx];
          for (int c = 0; c < scale; c++) *drow++ = v;
        }
      }
    }
  }
  return micros() - t0;
}

}  // namespace

void runBenchmark();

void setup() {
  auto cfg = M5.config();
  cfg.output_power = true;
  M5.begin(cfg);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  M5.Display.setRotation(3);
  M5.Display.setBrightness(200);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setFont(&fonts::FreeSansBold18pt7b);
  M5.Display.drawString("benchmarking...", 40, 40);

  delay(400);
}

// Repeated rather than run once at boot: output printed before a host attaches
// is dropped (Serial is non-blocking), so a one-shot benchmark is a race
// against connecting in time. This just runs again every 20 s.
void runBenchmark() {
  Serial.println();
  Serial.println("=== Tab5 display throughput ===");
  Serial.printf("panel %dx%d  PSRAM free %u KB  internal free %u KB\n",
                M5.Display.width(), M5.Display.height(),
                (unsigned)(ESP.getFreePsram() / 1024),
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
  Serial.println();

  // --- blit from a PSRAM framebuffer (what an emulator would realistically do)
  Serial.println("-- pushImage from PSRAM --");
  for (const Case &c : kCases) {
    uint16_t *buf = alloc((size_t)c.w * c.h, MALLOC_CAP_SPIRAM);
    if (!buf) {
      Serial.printf("%-22s alloc failed\n", c.name);
      continue;
    }
    fillPattern(buf, c.w, c.h, 0);
    report(c.name, c.w, c.h, timePush(buf, c.w, c.h), kFrames);
    heap_caps_free(buf);
  }

  // --- width sweep: is a 512-byte row stride the thing that hurts?
  Serial.println();
  Serial.println("-- width sweep around 768 (all from PSRAM) --");
  for (const Case &c : kSweep) {
    uint16_t *buf = alloc((size_t)c.w * c.h, MALLOC_CAP_SPIRAM);
    if (!buf) {
      Serial.printf("%-22s alloc failed\n", c.name);
      continue;
    }
    fillPattern(buf, c.w, c.h, 0);
    report(c.name, c.w, c.h, timePush(buf, c.w, c.h), kFrames);
    heap_caps_free(buf);
  }

  // --- 768 wide, but pushed as two 384-wide halves from separate buffers, so
  // the source stride is 768 bytes instead of 1536. If the sweep says stride is
  // the problem, this is the free workaround: same pixels, no padding waste.
  Serial.println();
  Serial.println("-- 768x720 as two 384-wide halves --");
  {
    uint16_t *l = alloc(384 * 720, MALLOC_CAP_SPIRAM);
    uint16_t *r = alloc(384 * 720, MALLOC_CAP_SPIRAM);
    if (l && r) {
      fillPattern(l, 384, 720, 0);
      fillPattern(r, 384, 720, 7);
      const int x = (M5.Display.width() - 768) / 2;
      const int y = (M5.Display.height() - 720) / 2;
      const uint32_t t0 = micros();
      for (int i = 0; i < kFrames; i++) {
        M5.Display.startWrite();
        M5.Display.pushImage(x, y, 384, 720, l);
        M5.Display.pushImage(x + 384, y, 384, 720, r);
        M5.Display.endWrite();
        M5.Display.waitDMA();
      }
      report("768x720 split", 768, 720, micros() - t0, kFrames);
    } else {
      Serial.println("   alloc failed");
    }
    if (l) heap_caps_free(l);
    if (r) heap_caps_free(r);
  }

  // --- same size from internal SRAM, to see if PSRAM reads are the limit
  Serial.println();
  Serial.println("-- pushImage from internal SRAM (strip) --");
  {
    const int w = 768, h = 64;  // 96 KB, comfortably in internal RAM
    uint16_t *buf = alloc((size_t)w * h, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (buf) {
      fillPattern(buf, w, h, 0);
      // Scale the result to a full 720-line frame for comparability.
      const uint32_t us = timePush(buf, w, h);
      report("768x64 strip", w, h, us, kFrames);
      Serial.printf("   -> implies %.1f fps for a full 768x720 frame\n",
                    1000000.0 / ((double)us / kFrames * (720.0 / h)));
      heap_caps_free(buf);
    } else {
      Serial.println("   internal alloc failed");
    }
  }

  // --- the software upscale an emulator pays before blitting
  Serial.println();
  Serial.println("-- nearest-neighbour upscale (CPU only, no blit) --");
  {
    uint16_t *src = alloc(256 * 240, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint16_t *dst = alloc(768 * 720, MALLOC_CAP_SPIRAM);
    if (src && dst) {
      fillPattern(src, 256, 240, 0);
      const uint32_t us = timeScale(src, 256, 240, dst, 3);
      Serial.printf("%-22s %7.2f ms/frame  (NES 256x240 -> 768x720)\n",
                    "3x scale to PSRAM", (double)us / kFrames / 1000.0);
    } else {
      Serial.println("   alloc failed");
    }
    if (src) heap_caps_free(src);
    if (dst) heap_caps_free(dst);
  }

  Serial.println();
  Serial.println("=== done ===");
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.drawString("done - see serial", 40, 40);
}

void loop() {
  runBenchmark();
  delay(20000);
}
