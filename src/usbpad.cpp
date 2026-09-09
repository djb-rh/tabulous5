#include "usbpad.h"

#include <M5Unified.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <usb/usb_host.h>
#include <esp_log.h>

#include <cstring>

namespace tabulous {
namespace usbpad {
namespace {

// ---- HID report layout, from the report descriptor -----------------------
//
// Only the parts a gamepad uses: where in the report the X and Y axes, the
// hat switch and the buttons sit, and how wide each is. Everything else in
// the descriptor is skipped over, which is most of it.
struct Field {
  int bit = -1;    // offset of the first bit in the report, or -1 if absent
  int size = 0;    // bits per item
  int32_t lo = 0, hi = 0;  // logical range
};

struct Layout {
  uint8_t report_id = 0;  // 0 when the device does not use report IDs
  Field x, y, hat;
  int buttons_bit = -1;
  int buttons = 0;
};

// The usage pairs the parser cares about.
constexpr uint16_t kPageDesktop = 0x01, kPageButton = 0x09;
constexpr uint16_t kUsageX = 0x30, kUsageY = 0x31, kUsageHat = 0x39;

// A one-pass walk of the descriptor's items, keeping enough global/local
// state to place each Input item. Report IDs restart the bit counter.
bool parseReportDescriptor(const uint8_t *d, int len, Layout *out) {
  Layout lay;
  int bitpos = 0;
  uint16_t page = 0;
  int32_t lmin = 0, lmax = 0;
  int rsize = 0, rcount = 0;
  uint16_t usages[16];
  int nusages = 0;
  int32_t umin = -1, umax = -1;
  bool found_any = false;

  for (int i = 0; i < len;) {
    const uint8_t prefix = d[i++];
    if (prefix == 0xFE) {  // long item: skip
      if (i + 1 >= len) break;
      const int dlen = d[i];
      i += 2 + dlen;
      continue;
    }
    const int sz = (prefix & 3) == 3 ? 4 : (prefix & 3);
    const uint8_t type = (prefix >> 2) & 3, tag = prefix >> 4;
    uint32_t v = 0;
    for (int k = 0; k < sz && i + k < len; k++) v |= (uint32_t)d[i + k] << (8 * k);
    int32_t sv = (int32_t)v;
    if (sz == 1) sv = (int8_t)v;
    else if (sz == 2) sv = (int16_t)v;
    i += sz;

    if (type == 1) {  // global
      switch (tag) {
        case 0: page = (uint16_t)v; break;
        case 1: lmin = sv; break;
        case 2: lmax = sv; break;
        case 7: rsize = (int)v; break;
        case 8:
          // A new report ID: fields from here on live in that report, and
          // its ID byte comes first. The first ID seen is the one used.
          if (lay.report_id == 0 && !found_any) {
            lay.report_id = (uint8_t)v;
            bitpos = 8;
          } else if ((uint8_t)v != lay.report_id) {
            // Fields of another report: ignore them by parking bitpos.
            bitpos = 1 << 20;
          }
          break;
        case 9: rcount = (int)v; break;
        default: break;
      }
    } else if (type == 2) {  // local
      switch (tag) {
        case 0:
          if (nusages < 16) usages[nusages++] = (uint16_t)v;
          break;
        case 1: umin = (int32_t)v; break;
        case 2: umax = (int32_t)v; break;
        default: break;
      }
    } else if (type == 0) {  // main
      if (tag == 8) {  // Input
        const bool constant = v & 1;
        if (!constant && bitpos < (1 << 20)) {
          if (page == kPageButton) {
            if (lay.buttons_bit < 0) {
              lay.buttons_bit = bitpos;
              lay.buttons = rcount;
              if (lay.buttons > kMaxButtons) lay.buttons = kMaxButtons;
            } else if (lay.buttons_bit + lay.buttons * rsize == bitpos && rsize == 1) {
              lay.buttons += rcount;  // a second run straight after the first
              if (lay.buttons > kMaxButtons) lay.buttons = kMaxButtons;
            }
            found_any = true;
          } else if (page == kPageDesktop) {
            // One usage per report item, in order; a Usage Minimum/Maximum
            // pair expands to a run.
            int n = nusages;
            uint16_t list[32];
            if (n == 0 && umin >= 0) {
              for (int32_t u = umin; u <= umax && n < 32; u++) list[n++] = (uint16_t)u;
            } else {
              for (int k = 0; k < n && k < 32; k++) list[k] = usages[k];
            }
            for (int k = 0; k < rcount; k++) {
              const uint16_t u = k < n ? list[k] : (n ? list[n - 1] : 0);
              Field *f = u == kUsageX ? &lay.x : u == kUsageY ? &lay.y
                                     : u == kUsageHat ? &lay.hat : nullptr;
              if (f && f->bit < 0) {
                f->bit = bitpos + k * rsize;
                f->size = rsize;
                f->lo = lmin;
                f->hi = lmax;
                found_any = true;
              }
            }
          }
        }
        bitpos += rsize * rcount;
      } else if (tag == 9 || tag == 11) {  // Output / Feature: not in the input report
      }
      nusages = 0;
      umin = umax = -1;
    }
  }
  if (!found_any) return false;
  *out = lay;
  return true;
}

uint32_t bits(const uint8_t *r, int len, int bit, int size) {
  uint32_t v = 0;
  for (int k = 0; k < size && k < 32; k++) {
    const int b = bit + k;
    if (b / 8 >= len) break;
    v |= (uint32_t)((r[b / 8] >> (b % 8)) & 1) << k;
  }
  return v;
}

// An axis to -1/0/+1: the middle third is centre, so a pad that rests at 127
// or 128 on a 0..255 range reads as still.
int8_t axisToDir(const Field &f, const uint8_t *r, int len) {
  if (f.bit < 0) return 0;
  int32_t v = (int32_t)bits(r, len, f.bit, f.size);
  int32_t lo = f.lo, hi = f.hi;
  if (lo < 0 && f.size < 32) {
    // signed field
    if (v & (1u << (f.size - 1))) v -= (1 << f.size);
  }
  if (hi <= lo) { lo = 0; hi = (1 << f.size) - 1; }
  const int32_t span = hi - lo;
  if (v - lo < span / 3) return -1;
  if (v - lo > span - span / 3) return 1;
  return 0;
}

// ---- device state -------------------------------------------------------

State g_state;
Layout g_layout;
bool g_layout_ok = false;
uint8_t g_last[64];
int g_last_len = 0;
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

usb_host_client_handle_t g_client = nullptr;
usb_device_handle_t g_dev = nullptr;
usb_transfer_t *g_xfer = nullptr;
uint8_t g_intf = 0xFF;
uint8_t g_ep_in = 0;
uint16_t g_ep_mps = 0;
bool g_began = false;

void applyReport(const uint8_t *r, int len) {
  if (len <= 0) return;
  if (g_layout.report_id && r[0] != g_layout.report_id) return;
  State s = g_state;
  s.reports++;
  if (g_layout_ok) {
    uint32_t down = 0;
    for (int b = 0; b < g_layout.buttons; b++) {
      if (bits(r, len, g_layout.buttons_bit + b, 1)) down |= 1u << b;
    }
    s.down = down;
    s.buttons = g_layout.buttons;
    int8_t x = axisToDir(g_layout.x, r, len);
    int8_t y = axisToDir(g_layout.y, r, len);
    if (g_layout.hat.bit >= 0) {
      // Hat: 0 = up, clockwise in eighths; anything past 7 is centred.
      const uint32_t h = bits(r, len, g_layout.hat.bit, g_layout.hat.size) - (uint32_t)g_layout.hat.lo;
      static const int8_t hx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
      static const int8_t hy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
      if (h < 8) {
        if (!x) x = hx[h];
        if (!y) y = hy[h];
      }
    }
    s.x = x;
    s.y = y;
  }
  portENTER_CRITICAL(&g_mux);
  g_state = s;
  g_last_len = len < (int)sizeof(g_last) ? len : (int)sizeof(g_last);
  memcpy(g_last, r, g_last_len);
  portEXIT_CRITICAL(&g_mux);
}

// A pad reports by one interrupt transfer at a time: each completion has to
// resubmit, or nothing is ever heard again. The submit can fail -- a stalled
// endpoint is the usual way -- and if that goes unnoticed the pad stays open,
// stays named, and never delivers another button. Which is indistinguishable,
// from the outside, from a game that has locked up. So a failed resubmit is
// recorded rather than dropped, and the client task picks it up.
volatile bool g_xfer_live = false;
volatile int g_xfer_last_bad = -1;

void onTransfer(usb_transfer_t *t) {
  if (t->status == USB_TRANSFER_STATUS_COMPLETED) {
    applyReport(t->data_buffer, t->actual_num_bytes);
  } else {
    g_xfer_last_bad = (int)t->status;
  }
  if (!g_dev || t->status == USB_TRANSFER_STATUS_NO_DEVICE ||
      t->status == USB_TRANSFER_STATUS_CANCELED) {
    g_xfer_live = false;
    return;
  }
  if (usb_host_transfer_submit(t) != ESP_OK) {
    g_xfer_live = false;  // the client task clears the endpoint and retries
  }
}

// GET_DESCRIPTOR(Report) on the interface, synchronously via a one-shot
// control transfer. HID puts the report descriptor behind the interface
// rather than the device, hence the recipient bits.
struct CtrlWait {
  volatile bool done;
};

void onControl(usb_transfer_t *t) {
  auto *w = (CtrlWait *)t->context;
  w->done = true;
}

// Runs the client's event pump until the transfer's callback has fired. Only
// valid from the client task, outside its own handle_events call.
bool waitControl(CtrlWait *w, uint32_t ms) {
  const uint32_t t0 = millis();
  while (!w->done) {
    if (millis() - t0 > ms) return false;
    usb_host_client_handle_events(g_client, pdMS_TO_TICKS(10));
  }
  return true;
}

bool fetchReportDescriptor(uint16_t len, uint8_t *out, int *got) {
  usb_transfer_t *ct = nullptr;
  if (usb_host_transfer_alloc(8 + len, 0, &ct) != ESP_OK) return false;
  usb_setup_packet_t *sp = (usb_setup_packet_t *)ct->data_buffer;
  sp->bmRequestType = 0x81;  // IN, standard, interface
  sp->bRequest = 0x06;       // GET_DESCRIPTOR
  sp->wValue = 0x2200;       // HID report descriptor, index 0
  sp->wIndex = g_intf;
  sp->wLength = len;
  ct->num_bytes = 8 + len;
  ct->device_handle = g_dev;
  ct->bEndpointAddress = 0;
  ct->callback = onControl;
  CtrlWait w{false};
  ct->context = &w;
  bool ok = false;
  const esp_err_t se = usb_host_transfer_submit_control(g_client, ct);
  if (se == ESP_OK && waitControl(&w, 1000) &&
      ct->status == USB_TRANSFER_STATUS_COMPLETED) {
    *got = ct->actual_num_bytes - 8;
    if (*got > 0) {
      memcpy(out, ct->data_buffer + 8, *got);
      ok = true;
    }
  } else {
    Serial.printf("usbpad: report descriptor request: submit=%d status=%d bytes=%d\n",
                  (int)se, (int)ct->status, ct->actual_num_bytes);
  }
  usb_host_transfer_free(ct);
  return ok;
}

void readString(uint8_t index, char *out, size_t cap) {
  out[0] = '\0';
  if (!index) return;
  usb_transfer_t *ct = nullptr;
  if (usb_host_transfer_alloc(8 + 64, 0, &ct) != ESP_OK) return;
  usb_setup_packet_t *sp = (usb_setup_packet_t *)ct->data_buffer;
  sp->bmRequestType = 0x80;
  sp->bRequest = 0x06;
  sp->wValue = (uint16_t)(0x0300 | index);
  sp->wIndex = 0x0409;
  sp->wLength = 64;
  ct->num_bytes = 8 + 64;
  ct->device_handle = g_dev;
  ct->bEndpointAddress = 0;
  ct->callback = onControl;
  CtrlWait w{false};
  ct->context = &w;
  if (usb_host_transfer_submit_control(g_client, ct) == ESP_OK &&
      waitControl(&w, 500) &&
      ct->status == USB_TRANSFER_STATUS_COMPLETED && ct->actual_num_bytes > 10) {
    // UTF-16LE to ASCII, dropping anything that is not.
    const uint8_t *s = ct->data_buffer + 8;
    const int n = (s[0] - 2) / 2;
    size_t o = 0;
    for (int k = 0; k < n && o + 1 < cap; k++) {
      const uint16_t c = s[2 + 2 * k] | (s[3 + 2 * k] << 8);
      if (c >= 0x20 && c < 0x7F) out[o++] = (char)c;
    }
    out[o] = '\0';
  }
  usb_host_transfer_free(ct);
}

void openDevice(uint8_t addr) {
  if (g_dev) return;
  if (usb_host_device_open(g_client, addr, &g_dev) != ESP_OK) return;

  const usb_device_desc_t *dd = nullptr;
  const usb_config_desc_t *cd = nullptr;
  if (usb_host_get_device_descriptor(g_dev, &dd) != ESP_OK ||
      usb_host_get_active_config_descriptor(g_dev, &cd) != ESP_OK) {
    usb_host_device_close(g_client, g_dev);
    g_dev = nullptr;
    return;
  }

  // First HID interface with an interrupt IN endpoint.
  int off = 0;
  const usb_intf_desc_t *intf = nullptr;
  const usb_ep_desc_t *ep = nullptr;
  uint16_t report_len = 0;
  for (int i = 0; i < cd->bNumInterfaces && !ep; i++) {
    off = 0;
    const usb_intf_desc_t *cand = usb_parse_interface_descriptor(cd, i, 0, &off);
    if (!cand || cand->bInterfaceClass != 0x03) continue;
    // The HID descriptor follows the interface descriptor and carries the
    // report descriptor's length.
    int hoff = off;
    const usb_standard_desc_t *h = usb_parse_next_descriptor_of_type(
        (const usb_standard_desc_t *)cand, cd->wTotalLength, 0x21, &hoff);
    if (h && h->bLength >= 9) {
      const uint8_t *hb = (const uint8_t *)h;
      report_len = (uint16_t)(hb[7] | (hb[8] << 8));
    }
    for (int e = 0; e < cand->bNumEndpoints; e++) {
      int eoff = off;
      const usb_ep_desc_t *c = usb_parse_endpoint_descriptor_by_index(cand, e, cd->wTotalLength, &eoff);
      if (c && (c->bEndpointAddress & 0x80) && (c->bmAttributes & 0x03) == 0x03) {
        ep = c;
        intf = cand;
        break;
      }
    }
  }
  if (!ep) {
    Serial.printf("usbpad: device %04x:%04x has no HID interrupt endpoint\n",
                  dd->idVendor, dd->idProduct);
    usb_host_device_close(g_client, g_dev);
    g_dev = nullptr;
    return;
  }

  g_intf = intf->bInterfaceNumber;
  g_ep_in = ep->bEndpointAddress;
  g_ep_mps = ep->wMaxPacketSize & 0x7FF;
  if (usb_host_interface_claim(g_client, g_dev, g_intf, intf->bAlternateSetting) != ESP_OK) {
    Serial.println("usbpad: claim failed");
    usb_host_device_close(g_client, g_dev);
    g_dev = nullptr;
    return;
  }

  State s;
  s.connected = true;
  s.vid = dd->idVendor;
  s.pid = dd->idProduct;
  readString(dd->iProduct, s.name, sizeof(s.name));
  if (!s.name[0]) snprintf(s.name, sizeof(s.name), "USB %04x:%04x", s.vid, s.pid);

  uint8_t rd[248];  // the stack's control transfers are capped at 256 bytes
  int got = 0;
  g_layout_ok = false;
  // A missing or oversized length in the HID descriptor is no reason to give
  // up: ask for as much as fits and take what comes back.
  uint16_t ask = report_len && report_len <= sizeof(rd) ? report_len : (uint16_t)sizeof(rd);
  if (fetchReportDescriptor(ask, rd, &got)) {
    g_layout_ok = parseReportDescriptor(rd, got, &g_layout);
    Serial.printf("usbpad: report descriptor %d bytes ->", got);
    if (g_layout_ok) {
      Serial.printf(" id=%u x@%d/%d y@%d/%d hat@%d/%d buttons=%d@%d\n",
                    g_layout.report_id, g_layout.x.bit, g_layout.x.size,
                    g_layout.y.bit, g_layout.y.size, g_layout.hat.bit,
                    g_layout.hat.size, g_layout.buttons, g_layout.buttons_bit);
    } else {
      Serial.println(" nothing usable (no axes, hat or buttons found)");
    }
  } else {
    Serial.println("usbpad: could not read the report descriptor");
  }
  s.buttons = g_layout_ok ? g_layout.buttons : 0;
  portENTER_CRITICAL(&g_mux);
  g_state = s;
  portEXIT_CRITICAL(&g_mux);
  Serial.printf("usbpad: %s (%04x:%04x) ep=%02x mps=%u\n", s.name, s.vid, s.pid,
                g_ep_in, g_ep_mps);

  if (!g_xfer) usb_host_transfer_alloc(64, 0, &g_xfer);
  if (g_xfer) {
    g_xfer->device_handle = g_dev;
    g_xfer->bEndpointAddress = g_ep_in;
    g_xfer->num_bytes = g_ep_mps > 64 ? 64 : g_ep_mps;
    g_xfer->callback = onTransfer;
    g_xfer->context = nullptr;
    const esp_err_t se = usb_host_transfer_submit(g_xfer);
    g_xfer_live = se == ESP_OK;
    if (!g_xfer_live) Serial.printf("usbpad: first listen failed -> %d\n", (int)se);
  }
}

uint32_t g_gone_at = 0;

// After DEV_GONE the device is already off the bus, so there is nothing to
// halt or flush: release what was claimed and close the handle, once.
void closeDevice() {
  usb_device_handle_t dev = g_dev;
  if (!dev) return;
  g_dev = nullptr;  // first, so a transfer callback racing this does not resubmit
  g_xfer_live = false;
  if (g_intf != 0xFF) usb_host_interface_release(g_client, dev, g_intf);
  usb_host_device_close(g_client, dev);
  g_intf = 0xFF;
  g_gone_at = millis();
  portENTER_CRITICAL(&g_mux);
  g_state = State{};
  portEXIT_CRITICAL(&g_mux);
  Serial.println("usbpad: gone");
}

// New devices are only noted here. Setting one up needs control transfers,
// whose completions arrive through the same event pump this callback is
// running inside of, so the work happens in the client loop instead.
volatile uint8_t g_pending_addr = 0;

void onClientEvent(const usb_host_client_event_msg_t *msg, void *) {
  if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    g_pending_addr = msg->new_dev.address;
  } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    closeDevice();
  }
}

// Two tasks, as the host library wants: one pumps the library itself, the
// other is the client. Both low priority; the pad is polled at 60 Hz by
// whoever cares, and a report arriving a millisecond late is not noticed.
void hostTask(void *) {
  for (;;) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(portMAX_DELAY, &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
  }
}

void clientTask(void *) {
  usb_host_client_config_t cfg = {};
  cfg.is_synchronous = false;
  cfg.max_num_event_msg = 5;
  cfg.async.client_event_callback = onClientEvent;
  cfg.async.callback_arg = nullptr;
  const esp_err_t rerr = usb_host_client_register(&cfg, &g_client);
  Serial.printf("usbpad: client register -> %d\n", (int)rerr);
  if (rerr != ESP_OK) {
    vTaskDelete(nullptr);
    return;
  }
  // A pad plugged in before boot is enumerated before this client exists,
  // and the library only announces devices that arrive afterwards. Ask for
  // what is already there.
  {
    uint8_t addrs[4];
    int n = 0;
    if (usb_host_device_addr_list_fill(sizeof(addrs), addrs, &n) == ESP_OK && n > 0) {
      Serial.printf("usbpad: %d device(s) already present\n", n);
      openDevice(addrs[0]);
    }
  }
  for (;;) {
    const esp_err_t e = usb_host_client_handle_events(g_client, pdMS_TO_TICKS(1000));
    if (e != ESP_OK && e != ESP_ERR_TIMEOUT) Serial.printf("usbpad: handle_events -> %d\n", (int)e);
    if (g_pending_addr) {
      const uint8_t a = g_pending_addr;
      g_pending_addr = 0;
      openDevice(a);
    }
    // A pad that is open but no longer listening: clear whatever halted the
    // endpoint and start it again. Without this a single stalled transfer
    // silences the pad until it is unplugged.
    if (g_dev && g_xfer && !g_xfer_live) {
      const int why = g_xfer_last_bad;
      g_xfer_last_bad = -1;
      usb_host_endpoint_clear(g_dev, g_ep_in);
      const esp_err_t se = usb_host_transfer_submit(g_xfer);
      g_xfer_live = se == ESP_OK;
      // Once when it happens and once when it comes back, then quietly every
      // half minute: this runs once a second and a pad that will not listen
      // must not fill the log.
      static uint32_t tries = 0;
      if (g_xfer_live || tries % 30 == 0) {
        Serial.printf("usbpad: listening again after status %d -> %s\n", why,
                      g_xfer_live ? "ok" : "failed, will retry");
      }
      tries = g_xfer_live ? 0 : tries + 1;
    }
    // Belt and braces: whatever the event delivery does, a device that is
    // there and not open gets opened.
    if (!g_dev && millis() - g_gone_at > 2000) {
      uint8_t addrs[4];
      int n = 0;
      if (usb_host_device_addr_list_fill(sizeof(addrs), addrs, &n) == ESP_OK && n > 0) {
        Serial.printf("usbpad: device at address %u, opening\n", addrs[0]);
        openDevice(addrs[0]);
      }
    }
  }
}

// Called by the stack mid-enumeration, once the device descriptor is in. Its
// real purpose is choosing a configuration; here it also says how far the
// handshake got, which the stack otherwise keeps to itself.
bool onEnumFilter(const usb_device_desc_t *dd, uint8_t *bConfigurationValue) {
  Serial.printf("usbpad: enumerating %04x:%04x usb=%04x class=%u ep0=%u configs=%u\n",
                dd->idVendor, dd->idProduct, dd->bcdUSB, dd->bDeviceClass,
                dd->bMaxPacketSize0, dd->bNumConfigurations);
  *bConfigurationValue = 1;
  return true;
}

}  // namespace

bool begin() {
  if (g_began) return true;

  usb_host_config_t cfg = {};
  cfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.enum_filter_cb = onEnumFilter;
  // The port stays unpowered until the client is listening: a pad that is
  // already plugged in at boot otherwise announces itself before anyone can
  // hear it, and the stack was seen to sit with the connection half made.
  cfg.root_port_unpowered = true;
#ifdef USBPAD_PERIPHERAL_MAP
  cfg.peripheral_map = USBPAD_PERIPHERAL_MAP;
#endif
  const esp_err_t err = usb_host_install(&cfg);
  if (err != ESP_OK) {
    Serial.printf("usbpad: usb_host_install failed: %d\n", (int)err);
    return false;
  }
  g_began = true;
  const BaseType_t a = xTaskCreatePinnedToCore(hostTask, "usbhost", 4096, nullptr, 2, nullptr, 0);
  const BaseType_t b = xTaskCreatePinnedToCore(clientTask, "usbpad", 8192, nullptr, 2, nullptr, 0);
  Serial.printf("usbpad: host installed, tasks %d/%d\n", (int)a, (int)b);
  vTaskDelay(pdMS_TO_TICKS(100));
  // 5 V to the USB-A port, through M5Unified's own expander driver, then the
  // root port itself.
  M5.Power.setExtOutput(true, m5::ext_port_mask_t::ext_USB);
  vTaskDelay(pdMS_TO_TICKS(50));
  usb_host_lib_set_root_port_power(true);
  return true;
}

State state() {
  portENTER_CRITICAL(&g_mux);
  State s = g_state;
  portEXIT_CRITICAL(&g_mux);
  return s;
}

int deviceCount() {
  usb_host_lib_info_t info = {};
  if (!g_began || usb_host_lib_info(&info) != ESP_OK) return -1;
  return info.num_devices;
}

int lastReport(uint8_t *out, int max) {
  portENTER_CRITICAL(&g_mux);
  const int n = g_last_len < max ? g_last_len : max;
  memcpy(out, g_last, n);
  portEXIT_CRITICAL(&g_mux);
  return n;
}

}  // namespace usbpad
}  // namespace tabulous
