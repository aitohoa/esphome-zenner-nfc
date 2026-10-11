// SPDX-License-Identifier: LGPL-2.1-or-later
#include <algorithm>
#include <cassert>
#include <iostream>

#include "../components/zenner_nfc/zenner_nfc.h"
#include "esphome/core/log.h"
using namespace esphome;
using namespace esphome::zenner_nfc;
class TestMeter : public ZennerNfc {
 public:
  bool idle() const { return scan_state_ == ScanState::IDLE; }
  bool ready() const { return ready_; }
  bool high_frequency() const { return high_frequency_.active; }
  bool at_gate() const { return scan_state_ == ScanState::WAIT_TX_GATE; }
  bool selecting() const { return scan_state_ == ScanState::SELECT_TAG; }
  bool stopping() const { return scan_state_ == ScanState::STOP_RF; }
  uint32_t failures() const { return consecutive_errors_; }
  void seed_failures(uint32_t n) { consecutive_errors_ = n; }
  unsigned attempts() const { return selection_attempt_; }
};
struct Fixture {
  GPIOPin nss{GPIOPin::NSS}, busy{GPIOPin::BUSY}, rst{GPIOPin::RST};
  TestMeter meter;
  text_sensor::TextSensor status;
  sensor::Sensor volume;
  binary_sensor::BinarySensor success;
  Fixture(uint8_t mode = 0, uint8_t control = 0x73, int fault = 0) {
    test_bus::clean();
    test_bus::leveled_logs.clear();
    test_bus::eeprom[0x59] = control;
    test_bus::ignore_eeprom_write = fault == 1;
    test_bus::fail_eeprom_write = fault == 2;
    test_bus::fail_dpc_read = fault == 3;
    test_bus::corrupt_on_reset = fault == 4;
    meter.set_cs_pin(&nss);
    meter.set_busy_pin(&busy);
    meter.set_reset_pin(&rst);
    meter.set_dpc_mode(mode);
    meter.set_status_sensor(&status);
    meter.set_volume_sensor(&volume);
    meter.set_read_success_sensor(&success);
    meter.setup();
    for (int i = 0; i < 5000 && !meter.ready() && !meter.failed; i++) tick();
    assert(meter.ready() || meter.failed);
    test_bus::scripted = true;
  }
  ~Fixture() { assert(!meter.enabled); }
  void tick() {
    auto before = test_bus::now, frames = test_bus::frame;
    meter.loop();
    assert(test_bus::now == before && test_bus::frame - frames <= 1 && !meter.enabled);
    test_bus::now++;
  }
  void finish() {
    for (int i = 0; i < 25000 && !meter.idle(); i++) tick();
    assert(meter.idle() && !meter.high_frequency());
  }
};
void selection_ok() {
  for (auto r : std::vector<test_bus::Reply>{
           {{0x52}, {4, 0}, 2}, {{0x93, 0x20}, {1, 2, 3, 4, 4}, 5}, {{0x93, 0x70, 1, 2, 3, 4, 4}, {0}, 1}})
    test_bus::replies.push_back(r);
}
unsigned count(uint8_t op) {
  unsigned n = 0;
  for (auto &v : test_bus::rf_commands)
    if (v[0] == op) n++;
  return n;
}
void session(uint8_t nc, uint8_t ns) {
  std::vector<uint8_t> b(16);
  b[0] = nc;
  b[6] = ns;
  test_bus::replies.push_back({{0x30, 0xEC}, b, 16});
}
void request(uint8_t fc, uint16_t seed, bool fail = false) {
  uint8_t req[6];
  iuw::request(req, fc, seed);
  std::vector<uint8_t> b(8);
  for (int i = 0; i < 6; i++) b[7 - i] = req[i];
  test_bus::replies.push_back({{0xA2, 0xFE, b[0], b[1], b[2], b[3]}, {10}, 0x8001});
  test_bus::replies.push_back({{0xA2, 0xFF, b[4], b[5], b[6], b[7]}, {uint8_t(fail ? 0 : 10)}, 0x8001});
}
void response(std::vector<uint8_t> b, uint16_t seed, bool corrupt = false) {
  auto crc = iuw::crc16(b.data(), b.size() - 2, seed);
  b[b.size() - 2] = crc;
  b.back() = crc >> 8;
  if (corrupt) b[5] ^= 1;
  b.resize(64);
  std::reverse(b.begin(), b.end());
  test_bus::replies.push_back({{0x3A, 0xF0, 0xFF}, b, 64});
}
void put32(std::vector<uint8_t> &b, int offset, uint32_t n) {
  for (int i = 0; i < 4; i++) b[offset + i] = n >> (8 * i);
}
void full_scan(bool corrupt = false) {
  selection_ok();
  session(0x41, 0x21);
  request(1, 0xFFFF);
  session(0x40, 0x29);
  session(0x40, 0x29);
  std::vector<uint8_t> id(48);
  id[0] = 45;
  id[1] = id[2] = id[3] = 1;
  put32(id, 17, 0x01085006);
  put32(id, 21, 0x12345678);
  response(id, 0xFFFF);
  session(0x41, 0x21);
  request(0x29, 0x444C);
  session(0x40, 0x29);
  session(0x40, 0x29);
  std::vector<uint8_t> v(48);
  v[0] = 45;
  v[1] = 0x29;
  double volume = 123.5;
  memcpy(v.data() + 9, &volume, 8);
  response(v, 0x444C, corrupt);
}
int main() {
  for (auto mode : {0, 1, 2}) {
    Fixture f(mode, mode == 2 ? 0x72 : 0x73);
    assert(!f.meter.failed);
    assert(test_bus::eeprom_writes == (mode == 0 ? 0u : 1u));
    assert(test_bus::eeprom[0x59] == (mode == 1 ? 0x72 : 0x73));
    assert(test_bus::eeprom[0x5A] == 0x20 && test_bus::eeprom[0x5B] == 0x4E);
    assert(test_bus::rf_commands.empty() && !f.meter.high_frequency());
  }
  {
    Fixture f(1, 0x72);
    assert(test_bus::eeprom_writes == 0);
  }
  for (int fault = 1; fault <= 4; fault++) {
    Fixture f(1, 0x73, fault);
    assert(f.meter.failed && !f.rst.value);
    f.meter.update();
    for (int i = 0; i < 100; i++) f.tick();
    assert(test_bus::rf_commands.empty() && !f.meter.high_frequency());
  }
  {
    Fixture f;
    full_scan();
    f.meter.update();
    assert(f.status.state == "reading");
    f.finish();
    assert(f.status.state == "ok" && f.success.state && f.volume.state == 123.5f);
    assert(test_bus::replies.empty() && count(0xA2) == 4);
  }
  {
    Fixture f;
    full_scan(true);
    f.meter.update();
    f.finish();
    assert(f.status.state == "invalid frame or CRC" && !f.success.state);
  }
  {
    Fixture f;
    test_bus::replies = {{{0x52}, {}, 0x40001}, {{0x52}, {}, 0x40001}};
    f.meter.update();
    f.finish();
    assert(f.meter.attempts() == 2 && count(0xA2) == 0 && !f.success.state);
  }
  {
    Fixture f;
    test_bus::replies = {{{0x52}, {}, 0x40001}};
    selection_ok();
    f.meter.update();
    for (int i = 0; i < 10000 && !f.meter.at_gate(); i++) {
      f.tick();
    }
    assert(f.meter.at_gate() && !f.meter.warning);
    assert(test_bus::rf_on_times.size() == 2 && test_bus::rf_off_times.size() == 1);
    assert(test_bus::rf_on_times[1] - test_bus::rf_off_times[0] >= 100);
    f.meter.on_shutdown();
    assert(f.nss.value && !f.rst.value);
  }
  {
    Fixture f;
    selection_ok();
    session(0x41, 0x21);
    request(1, 0xFFFF, true);
    f.meter.update();
    f.finish();
    assert(count(0xA2) == 2 && count(0x52) == 1 && f.status.state == "request write failed");
  }
  {
    Fixture f;
    selection_ok();
    session(0x41, 0x39);
    f.meter.update();
    f.finish();
    assert(count(0xA2) == 0 && f.status.state == "mailbox not empty; retry with next scan");
  }
  // Fault after RF comes on: no request replay, cooperative reset, next poll recovers.
  {
    Fixture f;
    f.meter.update();
    while (!f.meter.selecting()) f.tick();
    test_bus::busy = true;
    f.finish();
    assert(count(0xA2) == 0 && !f.success.state);
    full_scan();
    f.meter.update();
    f.finish();
    assert(f.success.state);
  }
  // Three failed scans invalidate numeric state; the next successful scan clears it.
  {
    Fixture f;
    for (int n = 0; n < 3; n++) {
      test_bus::replies = {{{0x52}, {}, 0x40001}, {{0x52}, {}, 0x40001}};
      f.meter.update();
      f.finish();
    }
    assert(std::isnan(f.volume.state));
    full_scan();
    f.meter.update();
    f.finish();
    assert(f.volume.state == 123.5f && !f.meter.warning);
  }
  // Missing antenna: one warning per scan, exact count beyond 255, one recovery log.
  {
    Fixture f;
    for (unsigned n = 0; n < 352; n++) {
      test_bus::replies = {{{0x52}, {}, 0, false}, {{0x52}, {}, 0, false}};
      f.meter.update();
      f.finish();
      assert(f.status.state == "NFC tag not responding");
      assert(f.meter.failures() == n + 1);
    }
    const std::string warning = "Tag selection failed: no response. Check antenna position and distance.";
    assert(std::count(test_bus::logs.begin(), test_bus::logs.end(), warning) == 352);
    assert(std::count_if(test_bus::leveled_logs.begin(), test_bus::leveled_logs.end(),
                         [](const auto &entry) { return entry.first == 'W'; }) == 352);
    assert(std::any_of(test_bus::leveled_logs.begin(), test_bus::leveled_logs.end(), [](const auto &entry) {
      return entry.first == 'D' && entry.second.find("failed at stage 2: IRQ response timeout") != std::string::npos;
    }));
    full_scan();
    f.meter.update();
    f.finish();
    assert(f.meter.failures() == 0 && !f.meter.warning && f.success.state);
    full_scan();
    f.meter.update();
    f.finish();
    const std::string restored = "NFC communication restored after 352 failed scans.";
    assert(std::count(test_bus::logs.begin(), test_bus::logs.end(), restored) == 1);
    assert(std::count(test_bus::leveled_logs.begin(), test_bus::leveled_logs.end(), std::make_pair('I', restored)) ==
           1);
    f.meter.seed_failures(UINT32_MAX);
    test_bus::replies = {{{0x52}, {}, 0, false}, {{0x52}, {}, 0, false}};
    f.meter.update();
    f.finish();
    assert(f.meter.failures() == UINT32_MAX);
  }
  // A read can succeed while RF shutdown fails; cleanup must reset and finish.
  {
    Fixture f;
    full_scan();
    f.meter.update();
    for (int i = 0; i < 20000 && !f.meter.stopping(); i++) {
      f.tick();
    }
    assert(f.meter.stopping());
    auto resets = test_bus::resets;
    test_bus::busy = true;
    f.finish();
    assert(f.success.state && test_bus::resets > resets && f.nss.value);
  }
  // Expire the global deadline while a request is pending: abort, reset, no replay.
  {
    Fixture f;
    selection_ok();
    session(0x41, 0x21);
    request(1, 0xFFFF);
    f.meter.update();
    for (int i = 0; i < 10000 && count(0xA2) == 0; i++) {
      f.tick();
    }
    assert(count(0xA2) == 1);
    test_bus::now += 21000;
    f.finish();
    assert(count(0xA2) == 1 && f.status.state == "scan timeout");
  }
  // Response deadline expires without replaying the completed two-page request.
  {
    Fixture f;
    selection_ok();
    session(0x41, 0x21);
    request(1, 0xFFFF);
    for (int i = 0; i < 40; i++) session(0x41, 0x21);
    f.meter.update();
    f.finish();
    assert(count(0xA2) == 2 && f.status.state == "response timeout" && !f.success.state);
  }
  // A failed recovery holds the reader in reset; a later scan can recover.
  {
    Fixture f;
    f.meter.update();
    for (int i = 0; i < 10000 && !f.meter.selecting(); i++) f.tick();
    assert(f.meter.selecting());
    test_bus::miss_high = true;
    f.finish();
    assert(!f.rst.value && !f.success.state);
    test_bus::miss_high = false;
    full_scan();
    f.meter.update();
    f.finish();
    assert(f.success.state);
  }
  std::cout
      << "Component tests passed: async startup/DPC, full two-command scan, retries, CRC, no replay and recovery.\n";
}
