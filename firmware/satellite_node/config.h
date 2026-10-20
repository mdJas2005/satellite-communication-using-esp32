#pragma once
// ---- Link to ground receiver (must match ground_receiver.ino) ----
#define WIFI_SSID   "SAT-GROUND"
#define WIFI_PASS   "change-me-123"      // CHANGE THIS
#define GROUND_IP   "192.168.4.1"
#define GROUND_PORT 3333

// ---- Timing ----
#define CAPTURE_INTERVAL_MS    10000
#define TELEMETRY_INTERVAL_MS  2000

// ---- Pins (AI-Thinker ESP32-CAM, SD card slot NOT used) ----
#define PIN_GPS_RX    13   // GPS module TX -> this pin
#define PIN_I2C_SDA   14   // BME280 + MPU6050
#define PIN_I2C_SCL   15
#define PIN_HAZARD_IN 2    // flame / hazard digital sensor, active LOW
#define PIN_BUZZER    4    // alarm buzzer / LED (shares onboard flash LED)
#define GPS_BAUD      9600

// ---- Onboard (autonomous) threat thresholds ----
#define TEMP_ALERT_C          60.0f   // overheating / fire
#define SHOCK_ALERT_G         2.5f    // impact / debris strike (total accel, g)
#define PRESSURE_DROP_HPA     15.0f   // drop vs. slow baseline (hull-breach demo)
#define ALERT_CONFIRM_SAMPLES 3       // consecutive samples before alert fires
#define ALERT_COOLDOWN_MS     60000   // min gap between repeat alerts of one kind
#define ALERT_FILE            "/alerts.jsonl"
