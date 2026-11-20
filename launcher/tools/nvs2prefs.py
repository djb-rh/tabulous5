#!/usr/bin/env python3
"""tools_nvs2prefs.py NVS.bin OUTDIR [namespace ...]: turns namespaces of a saved ESP32 NVS partition into the
.prefs files our ports' Preferences reads (/sd/tabulous/<namespace>.prefs), so a console's Tabulous5 settings
and score tables carry over to the launcher. Default namespaces: phrasecraze (settings), tab5scores.
If the console was taught a gamepad wiring (key bus_wire), that is written as tab5pad.txt, for /sd/config/.

The NVS partition of the classic firmware is at 0x9000, 0x5000 long:
    esptool --chip esp32p4 read-flash 0x9000 0x5000 nvs.bin"""
import os, struct, sys
d = open(sys.argv[1], "rb").read(); out = sys.argv[2]; want = sys.argv[3:] or ["phrasecraze", "tab5scores"]
SIZE = {0x01: 1, 0x11: 1, 0x02: 2, 0x12: 2, 0x04: 4, 0x14: 4, 0x08: 8, 0x18: 8}
ns = {}; vals = {}          # vals[nsi][key] = bytes ; blobs gathered by chunk
chunks = {}
for p in range(0, len(d), 4096):
    pg = d[p:p + 4096]
    if struct.unpack("<I", pg[:4])[0] == 0xFFFFFFFF: continue
    bm = pg[32:64]; i = 0
    while i < 126:
        e = pg[64 + i * 32:96 + i * 32]
        if (bm[i // 4] >> ((i % 4) * 2)) & 3 != 2: i += 1; continue
        nsi, typ, span, chunk = e[0], e[1], e[2], e[3]
        key = e[8:24].split(b"\0")[0].decode("latin1")
        if nsi == 0: ns[e[24]] = key
        elif typ in SIZE: vals.setdefault(nsi, {})[key] = e[24:24 + SIZE[typ]]
        elif typ in (0x21, 0x41, 0x42):
            size = struct.unpack("<H", e[24:26])[0]; data = pg[64 + (i + 1) * 32:64 + (i + 1) * 32 + size]
            if typ == 0x21: vals.setdefault(nsi, {})[key] = data.rstrip(b"\0")
            elif typ == 0x41: vals.setdefault(nsi, {})[key] = data
            else: chunks.setdefault((nsi, key), {})[chunk] = data
        i += max(span, 1)
for (nsi, key), parts in chunks.items(): vals.setdefault(nsi, {})[key] = b"".join(parts[c] for c in sorted(parts))
os.makedirs(out, exist_ok=True)
for nsi, name in sorted(ns.items()):
    if name not in want: continue
    recs = vals.get(nsi, {})
    with open(os.path.join(out, name + ".prefs"), "wb") as f:
        for k, v in recs.items():
            kb = k.encode(); f.write(bytes([len(kb)]) + kb + struct.pack("<I", len(v)) + v)
    print("%s.prefs: %d keys, %d bytes" % (name, len(recs), sum(len(v) for v in recs.values())))

# The rev A gamepad's wiring, as the classic firmware was taught it: twelve bytes, one per button in this order,
# each the position of its line in the header's twelve (src/busmap.h, kGpio), 0xFF for a button with no line.
BUTTONS = ["UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y", "L", "R", "SELECT", "START"]
GPIO = [16, 17, 45, 52, 7, 6, 3, 4, 2, 48, 47, 51]
for nsi, name in ns.items():
    wire = vals.get(nsi, {}).get("bus_wire") if name == "phrasecraze" else None
    if wire and len(wire) == 12:
        lines = ["%s=G%d" % (b, GPIO[l]) for b, l in zip(BUTTONS, wire) if l < 12]
        open(os.path.join(out, "tab5pad.txt"), "w").write("# Tab5 gamepad rev A wiring, from the console's own settings\n" + "\n".join(lines) + "\n")
        print("tab5pad.txt:", " ".join(lines))
