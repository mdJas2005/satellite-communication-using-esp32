/*
 * SATELLITE NODE  -  ESP32-CAM (AI-Thinker) + OV3660
 *
 * FreeRTOS tasks:
 *   gpsTask     (core0) parse NMEA from GPS module
 *   sensorTask  (core0) read BME280 / MPU6050 / hazard pin, run AUTONOMOUS threat detection
 *   cameraTask  (core1) capture JPEG every CAPTURE_INTERVAL_MS (keeps newest only)
 *   commTask    (core0) Wi-Fi/TCP link to ground receiver, telemetry, images, alert delivery
 *
 * Autonomy: threats are detected ON BOARD and written to flash (LittleFS). Alerts are re-sent
 * (with their original timestamps) until the ground receiver ACKs them, so a link outage never
 * loses a threat report.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <LittleFS.h>
#include <vector>
#include "esp_camera.h"
#include <TinyGPSPlus.h>
#include <Adafruit_BME280.h>
#include <Adafruit_MPU6050.h>
#include "config.h"
#include "protocol.h"

// ---- AI-Thinker ESP32-CAM camera pins ----
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

struct Telemetry {
  double lat, lon, alt;
  bool gpsValid;
  uint8_t sats;
  char utc[24];
  float tempC, hum, pressHpa, accelG;   // -999 = sensor missing / invalid
  bool hazardIn;
  bool threatActive;
};
struct ImageItem { uint8_t *buf; size_t len; };

static Telemetry g_tel = {};
static SemaphoreHandle_t g_telMux, g_fsMux;
static QueueHandle_t g_imgQ;
static bool g_camOk = false;
static uint16_t g_seq = 0;
static uint32_t g_alertCounter = 0, g_bootId = 0;

static Telemetry snapshot() {
  Telemetry s;
  xSemaphoreTake(g_telMux, portMAX_DELAY); s = g_tel; xSemaphoreGive(g_telMux);
  return s;
}

// ------------------------------------------------------------------ camera
static bool initCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0; c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM; c.pin_d1 = Y3_GPIO_NUM; c.pin_d2 = Y4_GPIO_NUM; c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM; c.pin_d5 = Y7_GPIO_NUM; c.pin_d6 = Y8_GPIO_NUM; c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM; c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM; c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM; c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM; c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000; c.pixel_format = PIXFORMAT_JPEG;
  if (psramFound()) {
    c.frame_size = FRAMESIZE_SVGA; c.jpeg_quality = 12; c.fb_count = 2;
    c.fb_location = CAMERA_FB_IN_PSRAM; c.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    c.frame_size = FRAMESIZE_QVGA; c.jpeg_quality = 15; c.fb_count = 1;
    c.fb_location = CAMERA_FB_IN_DRAM; c.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }
  if (esp_camera_init(&c) != ESP_OK) return false;
  sensor_t *s = esp_camera_sensor_get();
  if (s && s->id.PID == OV3660_PID) { s->set_vflip(s, 1); s->set_brightness(s, 1); s->set_saturation(s, -2); }
  return true;
}

static void cameraTask(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(CAPTURE_INTERVAL_MS));
    if (!g_camOk) continue;
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) continue;
    uint8_t *copy = psramFound() ? (uint8_t *)ps_malloc(fb->len) : (uint8_t *)malloc(fb->len);
    if (copy) {
      memcpy(copy, fb->buf, fb->len);
      ImageItem it = {copy, fb->len}, old;
      if (xQueueReceive(g_imgQ, &old, 0) == pdTRUE) free(old.buf);   // keep newest only
      if (xQueueSend(g_imgQ, &it, 0) != pdTRUE) free(copy);
    }
    esp_camera_fb_return(fb);
  }
}

// ------------------------------------------------------------------ GPS
static void gpsTask(void *) {
  TinyGPSPlus gps;
  Serial2.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, -1);
  for (;;) {
    while (Serial2.available()) gps.encode(Serial2.read());
    xSemaphoreTake(g_telMux, portMAX_DELAY);
    g_tel.gpsValid = gps.location.isValid();
    if (g_tel.gpsValid) { g_tel.lat = gps.location.lat(); g_tel.lon = gps.location.lng(); }
    g_tel.alt = gps.altitude.isValid() ? gps.altitude.meters() : 0;
    g_tel.sats = gps.satellites.isValid() ? gps.satellites.value() : 0;
    if (gps.date.isValid() && gps.time.isValid() && gps.date.year() > 2000)
      snprintf(g_tel.utc, sizeof g_tel.utc, "%04u-%02u-%02uT%02u:%02u:%02uZ", gps.date.year(), gps.date.month(),
               gps.date.day(), gps.time.hour(), gps.time.minute(), gps.time.second());
    else g_tel.utc[0] = 0;
    xSemaphoreGive(g_telMux);
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// ------------------------------------------------------------------ autonomous alerting
static void raiseAlert(const char *kind, float value) {
  Telemetry s = snapshot();
  char json[320];
  snprintf(json, sizeof json,
           "{\"id\":\"%08x-%lu\",\"kind\":\"%s\",\"severity\":\"CRITICAL\",\"value\":%.2f,\"uptime_ms\":%lu,"
           "\"utc\":\"%s\",\"gps_valid\":%d,\"lat\":%.6f,\"lon\":%.6f}",
           (unsigned)g_bootId, (unsigned long)++g_alertCounter, kind, value, (unsigned long)millis(), s.utc,
           s.gpsValid ? 1 : 0, s.lat, s.lon);
  Serial.printf("[ALERT] %s\n", json);
  xSemaphoreTake(g_fsMux, portMAX_DELAY);
  if (LittleFS.exists(ALERT_FILE)) {
    File chk = LittleFS.open(ALERT_FILE, "r");
    if (chk && chk.size() > 32768) { chk.close(); LittleFS.remove(ALERT_FILE); }   // cap: drop oldest backlog
    else if (chk) chk.close();
  }
  File f = LittleFS.open(ALERT_FILE, FILE_APPEND);
  if (f) { f.println(json); f.close(); }
  xSemaphoreGive(g_fsMux);
}

static void sensorTask(void *) {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Adafruit_BME280 bme;
  Adafruit_MPU6050 mpu;
  bool bmeOk = bme.begin(0x76, &Wire) || bme.begin(0x77, &Wire);
  bool mpuOk = mpu.begin(0x68, &Wire);
  Serial.printf("BME280:%d MPU6050:%d\n", bmeOk, mpuOk);

  const char *kinds[3] = {"FIRE_OR_OVERHEAT", "IMPACT_SHOCK", "PRESSURE_DROP"};
  int hits[3] = {0, 0, 0};
  uint32_t lastAlert[3] = {0, 0, 0};
  float basePress = NAN;

  for (;;) {
    float t = NAN, h = NAN, p = NAN, g = NAN;
    if (bmeOk) { t = bme.readTemperature(); h = bme.readHumidity(); p = bme.readPressure() / 100.0f; }
    if (mpuOk) {
      sensors_event_t a, gy, te;
      mpu.getEvent(&a, &gy, &te);
      g = sqrtf(a.acceleration.x * a.acceleration.x + a.acceleration.y * a.acceleration.y +
                a.acceleration.z * a.acceleration.z) / 9.80665f;
    }
    bool flame = digitalRead(PIN_HAZARD_IN) == LOW;
    if (!isnan(p) && isnan(basePress)) basePress = p;

    bool cond[3];
    cond[0] = (!isnan(t) && t > TEMP_ALERT_C) || flame;
    cond[1] = !isnan(g) && g > SHOCK_ALERT_G;
    cond[2] = !isnan(p) && !isnan(basePress) && (basePress - p) > PRESSURE_DROP_HPA;
    float val[3] = {flame ? 999.0f : t, g, isnan(p) ? 0 : basePress - p};
    if (!isnan(p) && !cond[2]) basePress = 0.995f * basePress + 0.005f * p;   // slow baseline

    bool active = false;
    uint32_t now = millis();
    for (int i = 0; i < 3; i++) {
      hits[i] = cond[i] ? hits[i] + 1 : 0;
      if (hits[i] >= ALERT_CONFIRM_SAMPLES) {
        active = true;
        if (lastAlert[i] == 0 || now - lastAlert[i] > ALERT_COOLDOWN_MS) { raiseAlert(kinds[i], val[i]); lastAlert[i] = now; }
      }
    }
    digitalWrite(PIN_BUZZER, active ? HIGH : LOW);

    xSemaphoreTake(g_telMux, portMAX_DELAY);
    g_tel.tempC = isnan(t) ? -999 : t; g_tel.hum = isnan(h) ? -999 : h;
    g_tel.pressHpa = isnan(p) ? -999 : p; g_tel.accelG = isnan(g) ? -999 : g;
    g_tel.hazardIn = flame; g_tel.threatActive = active;
    xSemaphoreGive(g_telMux);
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// ------------------------------------------------------------------ link
static bool sendFrame(WiFiClient &c, uint8_t type, uint16_t seq, const uint8_t *payload, uint32_t len) {
  uint8_t h[FRAME_HDR_LEN];
  build_header(h, type, seq, len);
  uint16_t crc = 0xFFFF;
  for (int i = 2; i < FRAME_HDR_LEN; i++) crc = crc16_update(crc, h[i]);
  for (uint32_t i = 0; i < len; i++) crc = crc16_update(crc, payload[i]);
  if (c.write(h, FRAME_HDR_LEN) != FRAME_HDR_LEN) return false;
  uint32_t off = 0;
  while (off < len) {
    size_t n = min((uint32_t)1024, len - off);
    if (c.write(payload + off, n) != n) return false;
    off += n;
  }
  uint8_t t[2] = {(uint8_t)(crc & 0xFF), (uint8_t)(crc >> 8)};
  return c.write(t, 2) == 2;
}

static bool waitAck(WiFiClient &c, uint16_t seq, uint32_t timeoutMs) {
  uint32_t t0 = millis();
  uint8_t buf[11];
  int n = 0;
  while (millis() - t0 < timeoutMs && c.connected()) {
    while (c.available()) {
      uint8_t b = c.read();
      if (n == 0 && b != SAT_MAGIC0) continue;
      if (n == 1 && b != SAT_MAGIC1) { n = 0; continue; }
      buf[n++] = b;
      if (n == 11) {
        n = 0;
        if (buf[2] == FT_ACK && (uint16_t)(buf[3] | (buf[4] << 8)) == seq) return true;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return false;
}

static bool flushAlerts(WiFiClient &c) {
  std::vector<String> lines;
  xSemaphoreTake(g_fsMux, portMAX_DELAY);
  if (!LittleFS.exists(ALERT_FILE)) { xSemaphoreGive(g_fsMux); return true; }
  File f = LittleFS.open(ALERT_FILE, "r");
  while (f && f.available()) { String l = f.readStringUntil('\n'); l.trim(); if (l.length()) lines.push_back(l); }
  if (f) f.close();
  LittleFS.remove(ALERT_FILE);            // new alerts during sending go to a fresh file
  xSemaphoreGive(g_fsMux);

  size_t i = 0;
  for (; i < lines.size(); i++) {
    uint16_t seq = ++g_seq;
    if (!sendFrame(c, FT_ALERT, seq, (const uint8_t *)lines[i].c_str(), lines[i].length()) || !waitAck(c, seq, 2000)) break;
  }
  if (i < lines.size()) {                  // put un-ACKed alerts back
    xSemaphoreTake(g_fsMux, portMAX_DELAY);
    File w = LittleFS.open(ALERT_FILE, FILE_APPEND);
    for (size_t j = i; j < lines.size(); j++) if (w) w.println(lines[j]);
    if (w) w.close();
    xSemaphoreGive(g_fsMux);
    return false;
  }
  return true;
}

static void commTask(void *) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFiClient client;
  uint32_t lastTel = 0;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      client.stop();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      uint32_t t0 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) vTaskDelay(pdMS_TO_TICKS(250));
      if (WiFi.status() != WL_CONNECTED) { WiFi.disconnect(); vTaskDelay(pdMS_TO_TICKS(3000)); continue; }
    }
    if (!client.connected()) {
      if (!client.connect(GROUND_IP, GROUND_PORT, 3000)) { vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
      client.setNoDelay(true);
      Serial.println("[LINK] connected");
    }
    if (!flushAlerts(client)) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }

    if (millis() - lastTel >= TELEMETRY_INTERVAL_MS) {
      Telemetry s = snapshot();
      char j[420];
      int n = snprintf(j, sizeof j,
          "{\"uptime_ms\":%lu,\"utc\":\"%s\",\"gps_valid\":%d,\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.1f,\"sats\":%u,"
          "\"temp_c\":%.2f,\"hum\":%.1f,\"press_hpa\":%.2f,\"accel_g\":%.2f,\"hazard_in\":%d,\"threat_active\":%d,\"rssi\":%d}",
          (unsigned long)millis(), s.utc, s.gpsValid, s.lat, s.lon, s.alt, s.sats, s.tempC, s.hum, s.pressHpa,
          s.accelG, s.hazardIn, s.threatActive, WiFi.RSSI());
      if (!sendFrame(client, FT_TELEMETRY, ++g_seq, (uint8_t *)j, n)) { client.stop(); continue; }
      lastTel = millis();
    }
    ImageItem it;
    if (xQueueReceive(g_imgQ, &it, 0) == pdTRUE) {
      bool ok = sendFrame(client, FT_IMAGE, ++g_seq, it.buf, it.len);
      free(it.buf);
      if (!ok) client.stop();
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ------------------------------------------------------------------ setup
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_HAZARD_IN, INPUT_PULLUP);
  g_bootId = esp_random();
  g_tel.tempC = g_tel.hum = g_tel.pressHpa = g_tel.accelG = -999;
  g_telMux = xSemaphoreCreateMutex();
  g_fsMux = xSemaphoreCreateMutex();
  g_imgQ = xQueueCreate(1, sizeof(ImageItem));
  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
  g_camOk = initCamera();
  Serial.printf("Camera: %s\n", g_camOk ? "OK" : "FAILED (sensors/alerts still run)");

  xTaskCreatePinnedToCore(gpsTask, "gps", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(sensorTask, "sensor", 8192, NULL, 3, NULL, 0);   // highest prio: threat detection
  xTaskCreatePinnedToCore(cameraTask, "cam", 6144, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(commTask, "comm", 8192, NULL, 2, NULL, 0);
}

void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }
