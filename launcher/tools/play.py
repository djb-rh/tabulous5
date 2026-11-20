# play.py APP [ROM path on the card] [seconds]: stop whatever runs, start APP, and for a while keep the app's
# log and count what the board pad's buttons do. For playing by hand with a real pad.
import sys, time, struct, json, threading
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import trial; pd = trial.pd
app = sys.argv[1]; args = [a for a in sys.argv[2:3] if a]; secs = float(sys.argv[3]) if len(sys.argv) > 3 else 300
dl = trial.connect()
if dl.json(pd.STATUS)["state"] != "idle":
    try: dl.request(pd.STOP, struct.pack("<I", 1500))
    except pd.DevLinkError as e: print("stop:", e)
    time.sleep(2.5)
remote = "/sd/apps/%s/%s.papp" % (app, app)
dl.request(pd.RUN, remote.encode() + b"\0" + b"".join(a.encode() + b"\0" for a in args), timeout=20.0)
print("started", remote, args, flush=True)
stop = threading.Event(); threading.Thread(target=pd.keepalive, args=(dl, stop), daemon=True).start()
t0 = time.time(); last = 0; presses = {}; chords = 0
while time.time() - t0 < secs:
    try: st = dl.json(pd.INPUT, bytes([pd.INPUT_STATE]), timeout=2)
    except pd.DevLinkError: time.sleep(0.3); continue
    b = 0
    for p in st["players"]:
        if p["connected"] and "oard" in p["name"]: b = p["buttons"]
    new = b & ~last
    if new:
        for n in pd.buttons_names(new).split(): presses[n] = presses.get(n, 0) + 1
        if bin(b).count("1") > 1: chords += 1
    last = b
    if time.time() - t0 > 20 and dl.json(pd.STATUS)["state"] == "idle": print("app ended at %.0f s" % (time.time() - t0)); break
    time.sleep(0.02)
stop.set()
print("presses by button:", json.dumps(presses), "| times two or more were held together:", chords, flush=True)
print("status:", json.dumps(dl.json(pd.STATUS)), flush=True)
dl.close()
