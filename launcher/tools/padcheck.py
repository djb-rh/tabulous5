# padcheck.py [wiring text | "-" to remove the file]: optionally (re)writes /sd/config/tab5pad.txt, restarts the
# launcher, and prints what it logged about the pad, the panel and its inputs.
import sys, time, json, os, tempfile
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import trial; pd = trial.pd
dl = trial.connect()
if len(sys.argv) > 1:
    if sys.argv[1] == "-":
        try: dl.request(pd.RM, b"/sd/config/tab5pad.txt"); print("removed the wiring file")
        except pd.DevLinkError as e: print("rm:", e)
    else:
        t = tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False, dir=os.path.join(trial.HERE, "logs")); t.write(sys.argv[1].replace("\\n", "\n")); t.close()
        dl.put(t.name, "/sd/config/tab5pad.txt", progress=False); os.unlink(t.name); print("wrote the wiring file")
    try: dl.request(pd.REBOOT, timeout=3)
    except pd.DevLinkError: pass
    dl.close(); time.sleep(8)
    for _ in range(30):
        try: dl = trial.connect(); break
        except Exception: time.sleep(0.5)
for k in range(8):
    try: log = dl.request(pd.BOOTLOG, timeout=5).decode("utf-8", "replace"); break
    except pd.DevLinkError: time.sleep(2)
for l in log.splitlines():
    if any(k in l for k in ("bsp_buttons", "board input", "bsp_disp: ST", "bsp_disp: ILI", "lx_input: player", "SD mounted")): print(l[:260])
st = dl.json(pd.INPUT, bytes([pd.INPUT_STATE]))
print("input state:", "devices", st["devices"], "| players:", [(p["name"], hex(p["buttons"])) for p in st["players"] if p["connected"]])
dl.close()
