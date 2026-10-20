# Satellite AI Ground Station (ESP32 prototype)

A working prototype of a **satellite node -> ground receiver -> laptop ground station** pipeline with
**AI image analysis** and **autonomous emergency alerting**. It is a prototype of the data flow, not a
claim that ESP32 boards talk to an orbiting satellite.

```
 SATELLITE NODE (ESP32-CAM + OV3660)          GROUND RECEIVER            GROUND STATION (laptop)
 camera | GPS | BME280 | MPU6050 | flame  --> ESP32-WROOM (Wi-Fi AP) --> receiver.py
 FreeRTOS tasks, onboard threat detection     bridges to USB serial       log + save images
 flash queue, retry until ACK                 CRC-checked, ACKs           AI inference + alerts
```

## Features
- Image capture (OV3660) + GPS position/time + environmental and shock sensing
- FreeRTOS tasks: GPS, sensors/threat-detection, camera, communication
- Framed, CRC-checked protocol; ground ACKs alerts
- **Onboard threat detection** (fire/overheat, impact, pressure drop) with local buzzer
- **Store-and-forward**: alerts persist in flash and are re-sent after the link returns
- Ground station: CSV logs, image archive, MobileNetV2 classifier, `LINK_LOST` / `LINK_RESTORED` alarms, optional webhook (phone/chat) notification
- Hardware-free simulator for demos

## Repository layout
```
firmware/satellite_node/   ESP32-CAM sketch (satellite_node.ino, config.h, protocol.h)
firmware/ground_receiver/  ESP32-WROOM sketch (ground_receiver.ino, protocol.h)
ground_station/            receiver.py, train.py, predict.py, alerts.py, simulate_satellite.py
dataset/                   your training images (one folder per class)
docs/                      ARCHITECTURE.md, HARDWARE.md
```

## Quick start (no hardware)
```bash
cd ground_station
pip install -r requirements.txt
python simulate_satellite.py --blackout-at 12 --blackout-for 22     # terminal 1
python receiver.py --port socket://localhost:5000 --no-ai            # terminal 2
```
You will see telemetry, images, satellite alerts, `LINK_LOST`, `LINK_RESTORED`, and queued alerts delivered after recovery. Output goes to `data/`.

## With hardware
1. Wire the node per [docs/HARDWARE.md](docs/HARDWARE.md).
2. Flash `firmware/ground_receiver` to the ESP32-WROOM; flash `firmware/satellite_node` to the ESP32-CAM. Change the Wi-Fi password in both.
3. Connect the WROOM to the laptop; run `python receiver.py --port COM3` (or `/dev/ttyUSB0`).

## Train the AI
Add images to `dataset/<class>/`, then `python train.py --epochs 10` (creates `model/`). Edit `THREAT_CLASSES` in `ground_station/config.py`.

## Roadmap
LoRa long-range link, TinyML on the node, LCD/web dashboard, encrypted frames, multi-node support.

## License
MIT
