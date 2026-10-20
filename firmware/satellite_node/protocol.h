#pragma once
// Shared frame format (identical copy lives in ground_receiver/ and ground_station/protocol.py)
// | AA 55 | type(1) | seq(2 LE) | len(4 LE) | payload(len) | crc16(2 LE) |
// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over type..payload.
#include <Arduino.h>

#define SAT_MAGIC0 0xAA
#define SAT_MAGIC1 0x55
#define FRAME_HDR_LEN 9
#define MAX_PAYLOAD 1000000UL

enum FrameType : uint8_t { FT_TELEMETRY = 1, FT_IMAGE = 2, FT_ALERT = 3, FT_ACK = 4 };

static inline uint16_t crc16_update(uint16_t crc, uint8_t b) {
  crc ^= (uint16_t)b << 8;
  for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  return crc;
}

static inline void build_header(uint8_t *h, uint8_t type, uint16_t seq, uint32_t len) {
  h[0] = SAT_MAGIC0; h[1] = SAT_MAGIC1; h[2] = type;
  h[3] = seq & 0xFF; h[4] = seq >> 8;
  h[5] = len & 0xFF; h[6] = (len >> 8) & 0xFF; h[7] = (len >> 16) & 0xFF; h[8] = (len >> 24) & 0xFF;
}
