# Dataset

Put training images in one folder per class, e.g.

```
dataset/
  normal/       *.jpg
  smoke_fire/   *.jpg
  flood/        *.jpg
```

- Class folder names become the AI labels. Emergency classes are listed in `ground_station/config.py` (`THREAT_CLASSES`).
- Best accuracy: include images captured **by your own ESP32-CAM/OV3660** (they are saved to `data/images/` while the ground station runs), so training matches deployment.
- Aim for 200+ images per class, varied lighting/angles. Then run `python train.py` from `ground_station/`.
