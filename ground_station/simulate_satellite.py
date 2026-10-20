"""Test the ground station WITHOUT hardware. Run this first, then:
     python receiver.py --port socket://localhost:5000
Sends telemetry (1 Hz), an image (every 5 s), a threat alert (every 20 s), and can simulate a link blackout
(--blackout-at 30 --blackout-for 25) to demo LINK_LOST / LINK_RESTORED and re-delivery of queued alerts.
"""
import argparse
import io
import json
import random
import socket
import time

from PIL import Image

from protocol import pack, T_TELEMETRY, T_IMAGE, T_ALERT

ap = argparse.ArgumentParser()
ap.add_argument("--port", type=int, default=5000)
ap.add_argument("--blackout-at", type=float, default=0)
ap.add_argument("--blackout-for", type=float, default=25)
ap.add_argument("--duration", type=float, default=0, help="stop after N seconds (0 = run forever)")
args = ap.parse_args()

srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", args.port))
srv.listen(1)
print("Waiting for receiver.py ...")
conn, _ = srv.accept()
print("Receiver connected.")

seq, n_alert, t0 = 0, 0, time.time()
pending = []                         # alerts raised during blackout, delivered afterwards
tick = 0
while True:
    el = time.time() - t0
    if args.duration and el > args.duration:
        break
    dark = args.blackout_at and args.blackout_at <= el < args.blackout_at + args.blackout_for
    if tick % 20 == 10:              # a threat occurs on board every 20 s
        n_alert += 1
        pending.append({"id": f"sim-{n_alert}",
                        "kind": random.choice(["FIRE_OR_OVERHEAT", "IMPACT_SHOCK", "PRESSURE_DROP"]),
                        "severity": "CRITICAL", "value": round(random.uniform(60, 90), 1),
                        "uptime_ms": int(el * 1000), "utc": "", "gps_valid": 1, "lat": 20.5937, "lon": 78.9629})
    if not dark:
        seq += 1
        tel = {"uptime_ms": int(el * 1000), "utc": "", "gps_valid": 1, "lat": 20.5937, "lon": 78.9629, "alt": 500,
               "sats": 8, "temp_c": round(random.uniform(24, 30), 2), "hum": 40.0, "press_hpa": 1008.1,
               "accel_g": 1.0, "hazard_in": 0, "threat_active": 0, "rssi": -55}
        conn.sendall(pack(T_TELEMETRY, seq, json.dumps(tel).encode()))
        if tick % 5 == 0:
            img = Image.new("RGB", (320, 240), tuple(random.randint(0, 255) for _ in range(3)))
            b = io.BytesIO()
            img.save(b, "JPEG")
            seq += 1
            conn.sendall(pack(T_IMAGE, seq, b.getvalue()))
        while pending:               # store-and-forward: deliver queued alerts once link is back
            seq += 1
            conn.sendall(pack(T_ALERT, seq, json.dumps(pending.pop(0)).encode()))
    tick += 1
    time.sleep(1)
conn.close()
