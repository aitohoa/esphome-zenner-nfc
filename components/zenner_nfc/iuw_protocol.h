// SPDX-License-Identifier: LGPL-2.1-or-later
// Modified for Zenner NFC on 2026-10-08; see NOTICE.md.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome {
namespace zenner_nfc {
namespace iuw {
inline uint16_t le16(const uint8_t *p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
inline uint32_t le32(const uint8_t *p) { return uint32_t(le16(p)) | (uint32_t(le16(p + 2)) << 16); }
inline uint16_t crc16(const uint8_t *p, size_t n, uint16_t seed) {
  uint16_t c = seed;
  for (size_t i = 0; i < n; i++) {
    c ^= uint16_t(p[i]) << 8;
    for (unsigned j = 0; j < 8; j++) c = (c & 0x8000) ? uint16_t((c << 1) ^ 0x1021) : uint16_t(c << 1);
  }
  return c;
}
inline void request(uint8_t out[6], uint8_t command, uint16_t seed) {
  out[0] = 3;
  out[1] = command;
  out[2] = 0x5A;
  out[3] = 0xA5;
  uint16_t c = crc16(out, 4, seed);
  out[4] = uint8_t(c);
  out[5] = uint8_t(c >> 8);
}
struct Frame {
  size_t size{0};
  uint8_t command{0};
  bool fallback_crc{false};
};
// One SRAM block only. Never search padding for a coincidentally valid frame.
inline bool frame(const uint8_t *p, size_t n, uint16_t seed, Frame &f) {
  f = Frame{};
  if (n < 4 || p[0] == 0 || p[0] == 0xFF) return false;
  size_t size = size_t(p[0]) + 3;
  if (size > n || size > 64) return false;
  uint16_t got = le16(p + size - 2);
  bool fallback = false;
  if (got != crc16(p, size - 2, seed)) {
    if (seed == 0xFFFF || got != crc16(p, size - 2, 0xFFFF)) return false;
    fallback = true;
  }
  f.size = size;
  f.command = p[1];
  f.fallback_crc = fallback;
  return true;
}
struct Identity {
  uint32_t meter_id{0}, firmware{0};
  uint16_t seed{0};
  uint8_t format{0};
};
inline bool identity(const uint8_t *p, const Frame &f, Identity &id) {
  if (f.command != 1 || f.size < 4 || p[3] != 1) return false;
  size_t fw = 0, mid = 0;
  // Deliberately accept only the two exact MiCon layouts. Other versions are raw-only.
  if (p[2] == 1 && f.size == 48) {
    fw = 17;
    mid = 21;
  } else if (p[2] == 2 && f.size == 50) {
    fw = 19;
    mid = 23;
  } else
    return false;
  uint32_t v = le32(p + fw);
  unsigned major = v >> 24, minor = (v >> 16) & 255, rev = (v >> 12) & 15;
  if (major < 1 || (major == 1 && (minor < 5 || (minor == 5 && rev < 1)))) return false;
  id.format = p[2];
  id.firmware = v;
  id.meter_id = le32(p + mid);
  id.seed = uint16_t(id.meter_id ^ (id.meter_id >> 16));
  return true;
}
struct Current {
  uint8_t unit{0};
  double volume{0}, forward{0}, reverse{0};
  float flow{0};
  bool has_temperatures{false}, has_scale{false};
  float water_temperature{0}, ambient_temperature{0};
  uint8_t scale_index{0};
};
inline double le_double(const uint8_t *p) {
  static_assert(sizeof(double) == 8, "64-bit double required");
  uint64_t bits = uint64_t(le32(p)) | (uint64_t(le32(p + 4)) << 32);
  double v;
  std::memcpy(&v, &bits, 8);
  return v;
}
inline float le_float(const uint8_t *p) {
  static_assert(sizeof(float) == 4, "32-bit float required");
  uint32_t bits = le32(p);
  float v;
  std::memcpy(&v, &bits, 4);
  return v;
}
inline bool current(const uint8_t *p, const Frame &f, Current &v) {
  if (f.command != 0x29 || (f.size != 43 && f.size != 48) || p[8] > 1) return false;
  v = Current{};
  v.unit = p[8];
  v.volume = le_double(p + 9);
  v.forward = le_double(p + 17);
  v.reverse = le_double(p + 25);
  v.flow = le_float(p + 33);
  // Observed 48-byte reply matches IUWCurrentData.Parse's optional tail.
  // Keep raw volume units; ScaleFactorIndex is display metadata, not a multiplier here.
  if (f.size == 48) {
    v.has_temperatures = true;
    v.has_scale = true;
    v.water_temperature = le_float(p + 37);
    v.ambient_temperature = le_float(p + 41);
    v.scale_index = p[45];
  }
  return std::isfinite(v.volume) && std::isfinite(v.forward) && std::isfinite(v.reverse) && std::isfinite(v.flow);
}
// unit: 0 = m3/h, 1 = L/min, 2 = L/s; input is always normalized m3/h.
inline double flow_in_unit(double m3_h, uint8_t unit) {
  return unit == 1 ? m3_h * (1000.0 / 60.0) : unit == 2 ? m3_h * (1000.0 / 3600.0) : m3_h;
}
struct Metric {
  double volume, forward, reverse, flow;
};
inline Metric metric(const Current &v) {
  constexpr double US_GALLON_M3 = 0.003785411784;
  const double factor = v.unit == 0 ? 1.0 : US_GALLON_M3;
  return {v.volume * factor, v.forward * factor, v.reverse * factor,
          v.unit == 0 ? double(v.flow) : double(v.flow) * US_GALLON_M3 * 60.0};
}
inline const char *error_name(uint16_t e) {
  switch (e) {
    case 0:
      return "OK";
    case 1:
      return "ERR_SIZE";
    case 2:
      return "ERR_CRC";
    case 3:
      return "ERR_CMD";
    case 4:
      return "ERR_WRTPRM";
    case 5:
      return "ERR_HAL";
    case 6:
      return "ERR_RDPRM";
    case 7:
      return "ERR_ISPROTECTED";
    case 8:
      return "ERR_PARAM";
    case 9:
      return "ERR_BUSY";
    default:
      return "UNKNOWN";
  }
}
}  // namespace iuw
}  // namespace zenner_nfc
}  // namespace esphome
