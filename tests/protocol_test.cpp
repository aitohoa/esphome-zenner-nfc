// SPDX-License-Identifier: LGPL-2.1-or-later
#include <cassert>
#include <iostream>
#include <limits>
#include <vector>

#include "../components/zenner_nfc/iuw_protocol.h"
using namespace esphome::zenner_nfc;
using Bytes = std::vector<uint8_t>;
void seal(Bytes& b, uint16_t seed) {
  auto c = iuw::crc16(b.data(), b.size() - 2, seed);
  b[b.size() - 2] = c;
  b.back() = c >> 8;
}
void put32(Bytes& b, size_t offset, uint32_t n) {
  for (unsigned i = 0; i < 4; i++) b[offset + i] = uint8_t(n >> (8 * i));
}
void put_float(Bytes& b, size_t offset, float n) {
  uint32_t bits;
  std::memcpy(&bits, &n, 4);
  put32(b, offset, bits);
}
void put_double(Bytes& b, size_t offset, double n) {
  uint64_t bits;
  std::memcpy(&bits, &n, 8);
  put32(b, offset, uint32_t(bits));
  put32(b, offset + 4, uint32_t(bits >> 32));
}
int main() {
  assert(iuw::flow_in_unit(3.6, 0) == 3.6);
  assert(std::abs(iuw::flow_in_unit(3.6, 1) - 60.0) < 1e-10);
  assert(std::abs(iuw::flow_in_unit(3.6, 2) - 1.0) < 1e-10);
  assert(std::abs(iuw::flow_in_unit(-3.6, 2) + 1.0) < 1e-10);
  // Independent fixed CRC/request vectors, then synthetic (non-identifying) frames.
  const uint8_t req1[] = {3, 1, 0x5A, 0xA5, 0x17, 0x2C};
  const uint8_t req29[] = {3, 0x29, 0x5A, 0xA5, 0xB0, 0x87};
  uint8_t r[6];
  iuw::request(r, 1, 0xFFFF);
  assert(std::memcmp(r, req1, 6) == 0);
  iuw::request(r, 0x29, 0);
  assert(std::memcmp(r, req29, 6) == 0);
  const uint8_t crc_vector[] = "123456789";
  assert(iuw::crc16(crc_vector, 9, 0xFFFF) == 0x29B1);
  Bytes b(48, 0);
  b[0] = 45;
  b[1] = 1;
  b[2] = 1;
  b[3] = 1;
  put32(b, 17, 0x01085006);
  put32(b, 21, 0x12345678);
  seal(b, 0xFFFF);
  iuw::Frame f;
  iuw::Identity id;
  assert(iuw::frame(b.data(), b.size(), 0xFFFF, f));
  assert(iuw::identity(b.data(), f, id));
  assert(id.meter_id == 0x12345678 && id.seed == 0x444C);
  for (size_t n = 0; n < b.size(); n++) assert(!iuw::frame(b.data(), n, 0xFFFF, f));
  for (size_t i = 0; i < b.size(); i++)
    for (unsigned bit = 0; bit < 8; bit++) {
      auto bad = b;
      bad[i] ^= 1u << bit;
      assert(!iuw::frame(bad.data(), bad.size(), 0xFFFF, f));
    }
  auto padded = b;
  padded.resize(64, 0xAA);
  assert(iuw::frame(padded.data(), 64, 0xFFFF, f) && f.size == 48);
  auto unknown = b;
  unknown[2] = 3;
  seal(unknown, 0xFFFF);
  assert(iuw::frame(unknown.data(), 48, 0xFFFF, f));
  assert(!iuw::identity(unknown.data(), f, id));
  auto v2 = b;
  v2.insert(v2.begin() + 17, 2, 0);
  v2[0] = 47;
  v2[2] = 2;
  seal(v2, 0xFFFF);
  assert(iuw::frame(v2.data(), 50, 0xFFFF, f));
  assert(iuw::identity(v2.data(), f, id) && id.seed == 0x444C);
  auto old = b;
  put32(old, 17, 0x01050006);
  seal(old, 0xFFFF);
  assert(iuw::frame(old.data(), 48, 0xFFFF, f));
  assert(!iuw::identity(old.data(), f, id));
  Bytes data(48, 0);
  data[0] = 45;
  data[1] = 0x29;
  put_double(data, 9, 123.5);
  put_double(data, 17, 125);
  put_double(data, 25, 1.5);
  put_float(data, 33, -0.25);
  put_float(data, 37, 15.25);
  put_float(data, 41, 22.0);
  data[45] = 6;
  seal(data, id.seed);
  iuw::Current current;
  assert(iuw::frame(data.data(), 48, id.seed, f));
  assert(iuw::current(data.data(), f, current));
  assert(current.volume == 123.5 && current.flow == -0.25 && current.has_temperatures);
  assert(current.water_temperature == 15.25 && current.ambient_temperature == 22 && current.scale_index == 6);
  auto metric = iuw::metric(current);
  assert(metric.volume == 123.5 && metric.flow == -0.25);
  // Decode display metadata without scaling an already absolute volume.
  data[45] = 2;
  seal(data, id.seed);
  assert(iuw::frame(data.data(), 48, id.seed, f));
  assert(iuw::current(data.data(), f, current) && current.volume == 123.5);
  current.unit = 1;
  current.volume = 1;
  current.forward = 2;
  current.reverse = 0.5;
  current.flow = 1;
  metric = iuw::metric(current);
  assert(std::abs(metric.volume - 0.003785411784) < 1e-12);
  assert(std::abs(metric.forward - 0.007570823568) < 1e-12);
  assert(std::abs(metric.flow - 0.22712470704) < 1e-12);
  // Older short reply must clear optional temperatures from a previous long reply.
  auto short_data = data;
  short_data.resize(43);
  short_data[0] = 40;
  seal(short_data, id.seed);
  assert(iuw::frame(short_data.data(), 43, id.seed, f));
  assert(iuw::current(short_data.data(), f, current) && !current.has_temperatures);
  seal(data, 0xFFFF);
  assert(iuw::frame(data.data(), 48, id.seed, f) && f.fallback_crc);
  data[8] = 2;
  seal(data, id.seed);
  assert(iuw::frame(data.data(), 48, id.seed, f));
  assert(!iuw::current(data.data(), f, current));
  data[8] = 0;
  put_double(data, 9, std::numeric_limits<double>::quiet_NaN());
  seal(data, id.seed);
  assert(iuw::frame(data.data(), 48, id.seed, f));
  assert(!iuw::current(data.data(), f, current));
  data[0] = 0xFF;
  assert(!iuw::frame(data.data(), 48, id.seed, f));
  for (size_t size : {size_t(42), size_t(44), size_t(45), size_t(46), size_t(47), size_t(49)}) {
    Bytes bad(size, 0);
    bad[0] = size - 3;
    bad[1] = 0x29;
    seal(bad, id.seed);
    assert(iuw::frame(bad.data(), size, id.seed, f));
    assert(!iuw::current(bad.data(), f, current));
  }
  std::cout << "Protocol tests passed: CRC vectors, all one-bit corruptions and truncations, identity "
               "layouts, optional temperatures, units, invalid numbers.\n";
}
