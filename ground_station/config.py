"""Ground-station settings. Edit here."""
import os
from pathlib import Path

BASE = Path(__file__).resolve().parent.parent
DATA_DIR = BASE / "data"
IMG_DIR = DATA_DIR / "images"
MODEL_DIR = BASE / "model"
MODEL_PATH = MODEL_DIR / "model.keras"
LABELS_PATH = MODEL_DIR / "labels.json"
DATASET_DIR = BASE / "dataset"

DEFAULT_PORT = "COM3"          # Linux: /dev/ttyUSB0   macOS: /dev/cu.usbserial-*
BAUD = 921600

# AI: class folder names (from dataset/) that count as an emergency
THREAT_CLASSES = {"smoke_fire", "flood"}
CONF_THRESHOLD = 0.80

# No frame for this long => LINK_LOST alert (last known state is reported)
LINK_TIMEOUT_S = 15

# Optional phone/chat notification: set env var ALERT_WEBHOOK_URL (Slack/Discord/ntfy-style JSON POST)
WEBHOOK_URL = os.environ.get("ALERT_WEBHOOK_URL", "")

for d in (DATA_DIR, IMG_DIR, MODEL_DIR):
    d.mkdir(parents=True, exist_ok=True)
