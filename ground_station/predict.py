"""Inference wrapper. Returns (label, confidence) for a JPEG byte string."""
import io
import json

import config


class Predictor:
    def __init__(self):
        import tensorflow as tf
        self.model = tf.keras.models.load_model(config.MODEL_PATH)
        self.labels = json.loads(config.LABELS_PATH.read_text())

    def predict(self, jpeg_bytes: bytes):
        import numpy as np
        from PIL import Image
        img = Image.open(io.BytesIO(jpeg_bytes)).convert("RGB").resize((224, 224))
        x = np.expand_dims(np.asarray(img, dtype="float32"), 0)   # scaling is inside the model
        p = self.model(x, training=False).numpy()[0]
        i = int(p.argmax())
        return self.labels[i], float(p[i])


def load_predictor():
    if not config.MODEL_PATH.exists():
        print("[AI] No trained model found (run train.py). Images will be saved without prediction.")
        return None
    try:
        return Predictor()
    except Exception as e:
        print(f"[AI] Could not load model: {e}")
        return None
