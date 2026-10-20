"""Alert fan-out: console, sound, CSV log, optional webhook."""
import csv
import json
import threading
from datetime import datetime

import config

FIELDS = ["time", "source", "severity", "kind", "message", "data"]


def log_csv(path, fields, row):
    new = not path.exists()
    with open(path, "a", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        if new:
            w.writeheader()
        w.writerow(row)


def _beep():
    try:
        import winsound
        winsound.Beep(1000, 300)
    except ImportError:
        print("\a", end="", flush=True)


def _webhook(text):
    try:
        import requests
        requests.post(config.WEBHOOK_URL, json={"text": text, "content": text}, timeout=5)
    except Exception as e:  # never let a notification failure break the receiver
        print(f"[webhook failed] {e}")


def raise_alert(source, kind, message, data=None, severity="CRITICAL"):
    now = datetime.now().isoformat(timespec="seconds")
    print(f"\n*** [{severity}] {now} {source}:{kind} - {message}\n")
    log_csv(config.DATA_DIR / "alerts.csv", FIELDS, {
        "time": now, "source": source, "severity": severity, "kind": kind,
        "message": message, "data": json.dumps(data or {}, default=str)})
    if severity == "CRITICAL":
        _beep()
    if config.WEBHOOK_URL:
        threading.Thread(target=_webhook, args=(f"[{severity}] {kind}: {message}",), daemon=True).start()
