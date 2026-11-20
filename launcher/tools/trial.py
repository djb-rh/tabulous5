#!/usr/bin/env python3
"""Drives a Tab5 that runs P4 Launcher, over USB: pushes sdcard/ to the card and runs each test, keeping the
app's log and screenshots under logs/trial/ (both in the work folder, TAB5_LAUNCHER_WORK).

    trial.py info                      what the launcher says about itself
    trial.py push [subdir ...]         copy sdcard/ (or parts of it) to /sd, only files that differ in size
    trial.py run NAME [NAME ...]       run tests by name (see TESTS), or `all`
    trial.py shot NAME                 one screenshot of whatever is on screen

Uses the launcher's own tools/papp_dev.py for the wire protocol.
"""
import json, os, struct, sys, threading, time
HERE = os.path.expanduser(os.environ.get("TAB5_LAUNCHER_WORK", "~/Developer/p4-launcher-test"))   # the launcher's clone, sdcard/, logs/
sys.path.insert(0, os.path.join(HERE, "ESP32_P4_Launcher", "tools"))
import papp_dev as pd  # noqa: E402

OUT = os.path.join(HERE, "logs", "trial")
SD = os.path.join(HERE, "sdcard")
R = "/sd/roms/"
# name: (app, [args], seconds to run, [(seconds after start, "pad"|"shot", value, hold ms)])
TESTS = {
    "nes":      ("nes", [R + "nes/Super Mario Bros. (World).nes"], 20, [(4, "pad", "START", 150), (8, "shot", "play", 0), (9, "pad", "RIGHT+B", 5000), (11, "pad", "RIGHT+B+A", 500), (13, "shot", "run", 0)]),
    "nes3":     ("nes", [R + "nes/Super Mario Bros. 3 (USA).nes"], 25, [(8, "pad", "START", 150), (12, "pad", "START", 150), (18, "shot", "map", 0)]),
    "gb":       ("gb", [R + "gb/Tetris (World) (Rev 1).gb"], 25, [(6, "pad", "START", 150), (9, "pad", "START", 150), (12, "pad", "START", 150), (15, "pad", "START", 150), (20, "shot", "play", 0)]),
    "gbzelda":  ("gb", [R + "gb/Legend of Zelda, The - Link's Awakening (USA, Europe).gb"], 25, [(10, "pad", "START", 150), (20, "shot", "title", 0)]),
    "snes":     ("snes", [R + "snes/Super Mario World (USA).sfc"], 40, [(8, "shot", "t8", 0), (16, "shot", "t16", 0), (24, "shot", "t24", 0), (34, "shot", "t34", 0)]),
    "snesfz":   ("snes", [R + "snes/F-Zero (USA).sfc"], 40, [(10, "pad", "START", 150), (14, "pad", "START", 150), (18, "pad", "START", 150), (22, "pad", "START", 150), (34, "shot", "race", 0)]),
    "doom":     ("doom", [], 45, [(8, "shot", "title", 0), (10, "pad", "A", 150), (12, "pad", "A", 150), (14, "pad", "A", 150), (16, "pad", "A", 150), (24, "pad", "UP", 3000), (30, "shot", "e1m1", 0)]),
    "quake":    ("quake", [], 60, [(20, "shot", "start", 0), (45, "shot", "demo", 0)]),
    "arcade":   ("arcade", [R + "arcade/Pac-Man (Midway).arc"], 60, [(8, "shot", "boot", 0), (27, "pad", "SELECT", 200), (29, "pad", "START", 200), (33, "pad", "LEFT", 2000), (36, "pad", "UP", 3000), (40, "shot", "play", 0), (41, "pad", "RIGHT", 3000)]),
    "arcq":     ("arcade", [R + "arcade/Pac-Man (Midway).arc"], 22, [(13, "shot", "t13", 0)]),
    "arcb1":    ("arcade", [R + "arcade/Pac-Man (Midway).arc", "1"], 17, []),
    "arcb2":    ("arcade", [R + "arcade/Pac-Man (Midway).arc", "2"], 17, []),
    "msq":      ("arcade", [R + "arcade/Ms. Pac-Man.arc"], 14, [(11, "shot", "t11", 0)]),
    "mspac":    ("arcade", [R + "arcade/Ms. Pac-Man.arc"], 45, [(27, "pad", "SELECT", 200), (29, "pad", "START", 200), (38, "shot", "play", 0)]),
    "mines":    ("minesweeper", [], 15, [(5, "shot", "board", 0)]),
    "sudoku":   ("sudoku", [], 15, [(5, "shot", "board", 0)]),
    "solitaire": ("solitaire", [], 15, [(5, "shot", "table", 0)]),
    "sound":    ("joshua", ["soundcheck"], 12, [(8, "shot", "after", 0)]),
    "phrase":   ("phrasecraze", [], 15, [(5, "shot", "home", 0)]),
    "fivehead": ("fivehead", [], 15, [(5, "shot", "home", 0)]),
    "joshua":   ("joshua", [], 25, [(5, "shot", "typing", 0), (20, "shot", "board", 0)]),
}


def connect(log_to=None):
    def on_log(app, text):
        line = ("[app] " if app else "") + text.rstrip("\n")
        print(line, flush=True)
        if log_to: log_to.write(line + "\n"); log_to.flush()
    return pd.DevLink(os.environ.get("PAPP_PORT") or pd.find_port(), on_log=on_log, on_event=lambda ev: on_log(False, "EVENT " + json.dumps(ev)))


def shot(dl, path):
    w, h, rgb = dl.shot(); pd.write_png(path, w, h, rgb); print("  shot ->", path, flush=True)


def push(dl, parts):
    made = set()
    def mkdirs(remote_dir):
        acc = ""
        for p in remote_dir.strip("/").split("/"):
            acc += "/" + p
            if acc == "/sd" or acc in made: continue
            try: dl.request(pd.MKDIR, acc.encode())
            except pd.DevLinkError: pass          # there already
            made.add(acc)
    total = 0; t0 = time.time()
    roots = [os.path.join(SD, p) for p in parts] if parts else [SD]
    for root in roots:
        for d, _, files in sorted(os.walk(root)):
            for f in sorted(files):
                if f.startswith("."): continue
                local = os.path.join(d, f); remote = "/sd/" + os.path.relpath(local, SD).replace(os.sep, "/")
                size = os.path.getsize(local)
                try:
                    st = dl.request(pd.STAT, remote.encode()).decode()
                    if json.loads(st).get("size") == size: print("  same size, skipped:", remote); continue
                except (pd.DevLinkError, ValueError): pass
                mkdirs(os.path.dirname(remote))
                dl.put(local, remote); total += size
    dt = time.time() - t0
    print("pushed %.1f MB in %.0f s (%.0f KB/s)" % (total / 1e6, dt, total / 1024 / max(dt, 1e-3)))


def run_test(name):
    app, args, secs, script = TESTS[name]
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, name + ".log"), "w") as lf:
        dl = connect(lf)
        try:
            st = dl.json(pd.STATUS)
            if st["state"] != "idle":
                dl.request(pd.STOP, struct.pack("<I", 1000)); time.sleep(2)
            remote = "/sd/apps/%s/%s.papp" % (app, app)
            payload = remote.encode() + b"\0" + b"".join(a.encode() + b"\0" for a in args)
            print("== %s: run %s %s" % (name, remote, args), flush=True)
            dl.request(pd.RUN, payload, timeout=20.0)
            stop_evt = threading.Event()
            threading.Thread(target=pd.keepalive, args=(dl, stop_evt), daemon=True).start()
            t0 = time.time()
            for at, kind, value, hold in sorted(script):
                while time.time() - t0 < at: time.sleep(0.05)
                try:
                    if kind == "shot": shot(dl, os.path.join(OUT, "%s_%s.png" % (name, value)))
                    else: pd.send_input(dl, kind, value, 0, hold)
                except pd.DevLinkError as e: print("  %s %s failed: %s" % (kind, value, e), flush=True)
            while time.time() - t0 < secs: time.sleep(0.1)
            st = dl.json(pd.STATUS); print("  status before stop:", json.dumps(st), flush=True); lf.write("status: " + json.dumps(st) + "\n")
            if st["state"] != "idle":
                try: print("  stop:", dl.request(pd.STOP, struct.pack("<I", 2000)).decode(), flush=True)
                except pd.DevLinkError as e: print("  stop failed:", e)
            time.sleep(2.5); stop_evt.set()
            try:
                cr = dl.json(pd.CRASH)
                if cr and cr.get("valid", cr.get("present", False)): lf.write("crash: " + json.dumps(cr) + "\n"); print("  CRASH REPORT:", json.dumps(cr)[:400])
            except (pd.DevLinkError, ValueError): pass
        finally:
            dl.close()


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "info"
    if cmd == "run":
        for n in (list(TESTS) if sys.argv[2:] == ["all"] else sys.argv[2:]): run_test(n); time.sleep(1)
        return
    dl = connect()
    try:
        if cmd == "info": print(json.dumps(dl.json(pd.INFO), indent=2))
        elif cmd == "push": push(dl, sys.argv[2:])
        elif cmd == "shot": os.makedirs(OUT, exist_ok=True); shot(dl, os.path.join(OUT, sys.argv[2] + ".png"))
        elif cmd == "ls": print(dl.request(pd.LS, sys.argv[2].encode()).decode())
    finally:
        dl.close()


if __name__ == "__main__":
    main()
