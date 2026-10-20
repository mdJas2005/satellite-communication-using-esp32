/*
 * GROUND RECEIVER - ESP32-WROOM
 * Creates a Wi-Fi access point, accepts the satellite node's TCP connection, and forwards every
 * frame unchanged to the laptop over USB serial. Valid frames (CRC ok) are ACKed back to the node.
 * NOTE: do not print debug text on Serial - it carries binary frames for the laptop.
 */
#include <WiFi.h>
#include "protocol.h"

#define AP_SSID   "SAT-GROUND"
#define AP_PASS   "change-me-123"     // must match satellite config.h
#define TCP_PORT  3333
#define SERIAL_BAUD 921600
#define LED_PIN   2

WiFiServer server(TCP_PORT);
WiFiClient client;
static uint8_t chunk[1024];

static bool readN(WiFiClient &c, uint8_t *dst, size_t n, uint32_t timeoutMs = 3000) {
  size_t got = 0;
  uint32_t t0 = millis();
  while (got < n) {
    int a = c.available();
    if (a > 0) {
      int r = c.read(dst + got, min((size_t)a, n - got));
      if (r > 0) { got += r; t0 = millis(); }
    } else {
      if (!c.connected() || millis() - t0 > timeoutMs) return false;
      delay(1);
    }
  }
  return true;
}

static void sendAck(uint16_t seq) {
  uint8_t h[FRAME_HDR_LEN + 2];
  build_header(h, FT_ACK, seq, 0);
  uint16_t crc = 0xFFFF;
  for (int i = 2; i < FRAME_HDR_LEN; i++) crc = crc16_update(crc, h[i]);
  h[9] = crc & 0xFF; h[10] = crc >> 8;
  client.write(h, sizeof h);
}

static void handleFrame() {
  if (client.peek() != SAT_MAGIC0) { client.read(); return; }   // resync
  uint8_t h[FRAME_HDR_LEN];
  if (!readN(client, h, FRAME_HDR_LEN) || h[1] != SAT_MAGIC1) return;
  uint32_t len = h[5] | (h[6] << 8) | (h[7] << 16) | ((uint32_t)h[8] << 24);
  if (len > MAX_PAYLOAD) return;

  uint16_t crc = 0xFFFF;
  for (int i = 2; i < FRAME_HDR_LEN; i++) crc = crc16_update(crc, h[i]);
  Serial.write(h, FRAME_HDR_LEN);

  uint32_t left = len;
  while (left) {                                   // stream through: no big buffer needed
    size_t n = min((uint32_t)sizeof chunk, left);
    if (!readN(client, chunk, n)) return;
    for (size_t i = 0; i < n; i++) crc = crc16_update(crc, chunk[i]);
    Serial.write(chunk, n);
    left -= n;
  }
  uint8_t t[2];
  if (!readN(client, t, 2)) return;
  Serial.write(t, 2);
  if ((uint16_t)(t[0] | (t[1] << 8)) == crc && h[2] != FT_ACK) sendAck(h[3] | (h[4] << 8));
  digitalWrite(LED_PIN, !digitalRead(LED_PIN));
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.setTxBufferSize(4096);
  Serial.begin(SERIAL_BAUD);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS, 6, 0, 2);          // channel 6, visible, max 2 clients
  server.begin();
  server.setNoDelay(true);
}

void loop() {
  if (!client || !client.connected()) {
    WiFiClient n = server.available();
    if (n) { client = n; client.setNoDelay(true); }
    else { delay(10); return; }
  }
  if (client.available() >= FRAME_HDR_LEN) handleFrame();
  else delay(1);
}
