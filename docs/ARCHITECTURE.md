# Architecture

```
ESP32-CAM node --(Wi-Fi TCP, CRC-framed)--> ESP32-WROOM AP --(USB serial 921600)--> Laptop
camera+GPS+sensors                           transparent bridge + ACK               receiver.py + AI
```

## Frame format (both links)
`AA 55 | type(1) | seq(2, LE) | len(4, LE) | payload | crc16(2, LE)`; CRC-16/CCITT-FALSE over type..payload.
Types: 1 telemetry (JSON), 2 image (JPEG), 3 alert (JSON), 4 ACK (empty payload, echoes seq).

## Autonomous threat detection (on the satellite node)
Rules run in a high-priority FreeRTOS task, independent of any link:
| Alert kind | Trigger |
|---|---|
| FIRE_OR_OVERHEAT | temp > `TEMP_ALERT_C` or flame pin active |
| IMPACT_SHOCK | acceleration > `SHOCK_ALERT_G` |
| PRESSURE_DROP | pressure falls > `PRESSURE_DROP_HPA` below slow baseline |

A rule must hold for `ALERT_CONFIRM_SAMPLES` samples, then it fires (cooldown `ALERT_COOLDOWN_MS`), sounds the local buzzer, and appends the alert to flash (`/alerts.jsonl`).

## Behaviour when the link is lost
1. Node keeps sensing and alerting; alerts accumulate in flash (power-cycle safe, capped ~32 KB).
2. When Wi-Fi/TCP returns, the node sends the backlog first; each alert is deleted only after the ground ACKs it, with its original uptime/GPS time and position.
3. Ground station dedupes alerts by id, and a watchdog raises `LINK_LOST` (with last-known position/state) after `LINK_TIMEOUT_S` of silence, then `LINK_RESTORED`.
4. AI threat alerts come from images; images are not queued during an outage (only the newest is sent on reconnect).

## AI path
`train.py` fine-tunes MobileNetV2 on `dataset/<class>/`. `receiver.py` runs each received JPEG through the model; classes in `THREAT_CLASSES` above `CONF_THRESHOLD` raise an alert (console, sound, CSV, optional webhook).

## Limits of the prototype (be upfront in your report)
- Wi-Fi range is tens of metres; this models the data flow of a satellite link, not orbital RF. A real mission would use LoRa/UHF/S-band and a ground-station pass scheduler.
- "Alert after the satellite is disconnected" is implemented as onboard detection + store-and-forward + ground link-loss alarm. A truly dead node cannot send anything; the ground side can only report the loss and last-known state.
- The threshold rules are demonstration-grade; tune them on real sensor data.
- Firmware was written to the documented ESP32 Arduino APIs but not compiled/flashed in the build environment; compile and test on your boards.
