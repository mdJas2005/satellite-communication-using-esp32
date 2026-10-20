# Hardware & Wiring

## Bill of materials
| Part | Role |
|---|---|
| ESP32-CAM (AI-Thinker, PSRAM) + OV3660 | Satellite node |
| GPS module (e.g. NEO-6M) + antenna | Position / time |
| BME280 | Temperature, humidity, pressure |
| MPU6050 | Shock / impact (acceleration) |
| Flame / hazard sensor (digital out) | Fire flag |
| Active buzzer (or LED) | Local alarm |
| ESP32-WROOM dev board (WROOM-32U for external antenna) | Ground receiver |
| FTDI/USB-UART adapter | Programming the ESP32-CAM |
| Stable 5 V supply, >= 1 A | ESP32-CAM brown-outs are the #1 cause of random resets |

## Satellite node pin map (AI-Thinker ESP32-CAM)
The SD-card slot is **not usable** in this design because its GPIOs are reused.

| ESP32-CAM pin | Connected to |
|---|---|
| GPIO13 | GPS TX (ESP32 receives) |
| GPIO14 | I2C SDA (BME280, MPU6050) |
| GPIO15 | I2C SCL |
| GPIO2  | Flame sensor DO (active LOW) |
| GPIO4  | Buzzer / LED (shares the onboard flash LED) |
| 5V / GND | Power; share ground with all modules |
| GPIO0 -> GND | Only while uploading firmware |

I2C addresses: BME280 0x76/0x77, MPU6050 0x68. Run the sensors from 3.3 V unless the breakout says otherwise.

## Antennas
- ESP32-CAM: the board has an onboard PCB antenna and a u.FL connector. Selecting the external antenna means moving the small 0-ohm resistor on the board (check your board's silkscreen/schematic). Use only one at a time.
- Ground ESP32: use a WROOM-32U style board for a u.FL external antenna.
- GPS: use an active antenna with clear sky view; first fix can take minutes.

## Libraries (Arduino IDE)
ESP32 board package (esp32 by Espressif), TinyGPSPlus, Adafruit BME280, Adafruit MPU6050 (+ Adafruit Unified Sensor, Adafruit BusIO).

Boards: satellite = "AI Thinker ESP32-CAM" (enable PSRAM); ground = "ESP32 Dev Module".

## Known risk to verify on hardware
The camera driver has its own I2C bus on GPIO26/27 while the sensors use `Wire` on GPIO14/15. If the sensors fail to initialise after camera init, switch the sensor bus to `Wire1`.
