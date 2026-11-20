# padwatch.py [seconds]: watch the launcher's player-1 buttons and print every change, until each of the pad's
# twelve buttons has been seen pressed and released (or the time runs out). For trying a real pad by hand.
import sys, time
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import trial; pd = trial.pd
WANT = ["UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y", "L1", "R1", "SELECT", "START"]
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 300
dl = trial.connect()
seen, last, t0, most = set(), 0, time.time(), 0
st = dl.json(pd.INPUT, bytes([pd.INPUT_STATE]))
print("devices:", st["devices"], "| players:", [p["name"] for p in st["players"] if p["connected"]], flush=True)
while time.time() - t0 < secs:
    try: st = dl.json(pd.INPUT, bytes([pd.INPUT_STATE]), timeout=2)
    except pd.DevLinkError as e: print("  (no answer: %s)" % e, flush=True); time.sleep(0.5); continue
    b = 0
    for p in st["players"]:
        if p["connected"] and "oard" in p["name"]: b = p["buttons"]
    if b != last:
        names = pd.buttons_names(b)
        held = [n for n in names.split() if n != "-"]
        print("%7.2f  %-5s %s" % (time.time() - t0, hex(b), "+".join(held) or "(released)"), flush=True)
        most = max(most, len(held))
        if len(held) == 1: seen.add(held[0])
        last = b
        if b == 0 and all(w in seen for w in WANT): break
    time.sleep(0.01)
print("seen alone:", [w for w in WANT if w in seen], flush=True)
print("NOT seen:", [w for w in WANT if w not in seen], "| other names seen:", sorted(seen - set(WANT)), "| most held at once:", most, flush=True)
dl.close()
