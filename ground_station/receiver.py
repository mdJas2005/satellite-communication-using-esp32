"""GROUND STATION. Reads frames from the ESP32-WROOM (USB serial), logs telemetry, saves images,
runs AI inference, raises alerts (satellite-reported, AI-detected, and link-loss).

  python receiver.py --port COM3
  python receiver.py --port socket://localhost:5000      # with simulate_satellite.py
"""
import argparse
import json
import threading
import time
from datetime import datetime

import serial

import config
from alerts import raise_alert, log_csv
from predict import load_predictor
from protocol import FrameReader, T_TELEMETRY, T_IMAGE, T_ALERT

TEL_FIELDS = ["rx_time", "utc", "gps_valid", "lat", "lon", "alt", "sats", "temp_c", "hum", "press_hpa",
              "accel_g", "hazard_in", "threat_active", "rssi", "uptime_ms"]
PRED_FIELDS = ["rx_time", "image", "lat", "lon", "label", "confidence"]

lock = threading.Lock()
state = {"last_rx": time.time(), "got_any": False, "telemetry": {}, "seen_alerts": set()}


def now_iso():
    return datetime.now().isoformat(timespec="seconds")


def on_telemetry(payload):
    try:
        t = json.loads(payload)
    except ValueError:
        return
    t = {k: (None if v == -999 else v) for k, v in t.items()}      # -999 = sensor missing
    t["rx_time"] = now_iso()
    with lock:
        state["telemetry"] = t
    log_csv(config.DATA_DIR / "telemetry.csv", TEL_FIELDS, t)
    print(f"[TEL] T={t.get('temp_c')}C P={t.get('press_hpa')}hPa g={t.get('accel_g')} "
          f"GPS={'fix' if t.get('gps_valid') else 'no fix'} ({t.get('lat')},{t.get('lon')})")


def on_image(payload, predictor):
    ts = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    path = config.IMG_DIR / f"{ts}.jpg"
    path.write_bytes(payload)
    with lock:
        tel = dict(state["telemetry"])
    label = conf = None
    if predictor:
        try:
            label, conf = predictor.predict(payload)
        except Exception as e:
            print(f"[AI] inference failed: {e}")
    row = {"rx_time": now_iso(), "image": path.name, "lat": tel.get("lat"), "lon": tel.get("lon"),
           "label": label, "confidence": None if conf is None else round(conf, 3)}
    log_csv(config.DATA_DIR / "predictions.csv", PRED_FIELDS, row)
    print(f"[IMG] {path.name} ({len(payload)//1024} KB)  AI: {label} {conf if conf is None else f'{conf:.0%}'}")
    if label in config.THREAT_CLASSES and conf is not None and conf >= config.CONF_THRESHOLD:
        raise_alert("ai", f"AI_{label.upper()}", f"{label} detected ({conf:.0%}) near "
                    f"{tel.get('lat')},{tel.get('lon')}", row)


def on_alert(payload):
    try:
        a = json.loads(payload)
    except ValueError:
        return
    aid = a.get("id")
    with lock:
        if aid in state["seen_alerts"]:          # retransmission of an already-handled alert
            return
        state["seen_alerts"].add(aid)
    raise_alert("satellite", a.get("kind", "UNKNOWN"),
                f"value={a.get('value')} at {a.get('utc') or 'uptime ' + str(a.get('uptime_ms'))} "
                f"pos={a.get('lat')},{a.get('lon')} (gps_valid={a.get('gps_valid')})", a)


def watchdog():
    lost = False
    while True:
        time.sleep(1)
        with lock:
            started, silent, last = state["got_any"], time.time() - state["last_rx"], dict(state["telemetry"])
        if started and silent > config.LINK_TIMEOUT_S and not lost:
            lost = True
            raise_alert("ground", "LINK_LOST", f"No data for {silent:.0f}s. Last known: "
                        f"pos={last.get('lat')},{last.get('lon')} T={last.get('temp_c')} "
                        f"threat_active={last.get('threat_active')}", last)
        elif lost and silent < config.LINK_TIMEOUT_S:
            lost = False
            raise_alert("ground", "LINK_RESTORED", "Link recovered; queued alerts will follow",
                        severity="INFO")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=config.DEFAULT_PORT)
    ap.add_argument("--baud", type=int, default=config.BAUD)
    ap.add_argument("--no-ai", action="store_true")
    args = ap.parse_args()

    predictor = None if args.no_ai else load_predictor()
    ser = serial.serial_for_url(args.port, baudrate=args.baud, timeout=1)
    reader = FrameReader(ser)
    threading.Thread(target=watchdog, daemon=True).start()
    print(f"Ground station listening on {args.port}. Ctrl+C to stop.")
    try:
        while True:
            fr = reader.read_frame()
            if fr is None:
                continue
            with lock:
                state["last_rx"], state["got_any"] = time.time(), True
            ftype, _seq, payload = fr
            if ftype == T_TELEMETRY:
                on_telemetry(payload)
            elif ftype == T_IMAGE:
                on_image(payload, predictor)
            elif ftype == T_ALERT:
                on_alert(payload)
    except KeyboardInterrupt:
        print(f"\nStopped. Corrupt frames dropped: {reader.bad_frames}")
    except serial.SerialException as e:      # USB unplugged / socket closed
        print(f"\nSerial connection lost: {e}")


if __name__ == "__main__":
    main()
