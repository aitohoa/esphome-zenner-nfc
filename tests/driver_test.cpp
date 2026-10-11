// SPDX-License-Identifier: LGPL-2.1-or-later
#include <cassert>
#include <iostream>

#include "../components/zenner_nfc/pn5180.h"
using namespace esphome;
using namespace esphome::zenner_nfc;
struct Fixture {
  GPIOPin nss{GPIOPin::NSS}, busy{GPIOPin::BUSY}, rst{GPIOPin::RST};
  PN5180 dev;
  Fixture() {
    test_bus::clean();
    dev.set_cs_pin(&nss);
    dev.set_busy_pin(&busy);
    dev.set_reset_pin(&rst);
    dev.begin();
  }
  ~Fixture() { assert(!dev.enabled); }
  void tick() {
    const auto before = test_bus::now, frames = test_bus::frame;
    dev.tick();
    assert(test_bus::now == before);        // No delay() or hidden busy-wait advancing fake time.
    assert(test_bus::frame - frames <= 1);  // At most one bounded SPI frame per invocation.
    assert(!dev.enabled);                   // Never hold the bus lock while returning to ESPHome.
    test_bus::now++;
  }
  void finish() {
    for (int i = 0; i < 5000 && dev.active(); i++) tick();
    assert(!dev.active());
  }
};
int main() {
  // Complete five-phase BUSY fault coverage, including NSS cleanup and reset recovery.
  for (int fault = 0; fault < 6; fault++) {
    Fixture f;
    if (fault == 1) test_bus::busy = true;
    if (fault == 2) test_bus::miss_high = true;
    if (fault == 3) test_bus::stall_frame = 1;
    if (fault == 5) test_bus::stall_frame = 2;
    f.dev.start_eeprom_read(0x10, 6);
    if (fault == 4) {
      while (test_bus::writes == 0) f.tick();
      test_bus::miss_high = true;
    }
    f.finish();
    assert(f.dev.ok() == (fault == 0));
    assert(f.dev.transport_failed() == (fault != 0));
    assert(f.nss.value);
    if (fault != 0) {
      auto writes = test_bus::writes;
      f.dev.start_dpc_write(0x72);
      f.finish();
      assert(test_bus::writes == writes);
      test_bus::miss_high = false;
      test_bus::stall_frame = 0;
      f.dev.start_reset();
      f.finish();
      assert(f.dev.ok() && !f.dev.transport_failed());
    }
  }
  // Delayed availability is cooperative; deadline works across millis wrap.
  {
    Fixture f;
    test_bus::busy = true;
    test_bus::ready_at = 25;
    f.dev.start_dpc_write(0x72);
    f.finish();
    assert(f.dev.ok() && test_bus::eeprom[0x59] == 0x72);
  }
  {
    Fixture f;
    test_bus::now = 0xFFFFFFF0u;
    test_bus::busy = true;
    f.dev.start_dpc_write(0x72);
    f.finish();
    assert(!f.dev.ok() && test_bus::eeprom_writes == 0);
  }
  // All UID cascade commands and BCC checks are exercised against scripted RF replies.
  for (int kind = 0; kind < 3; kind++) {
    Fixture f;
    test_bus::scripted = true;
    if (kind == 0)
      test_bus::replies = {
          {{0x52}, {4, 0}, 2}, {{0x93, 0x20}, {1, 2, 3, 4, 4}, 5}, {{0x93, 0x70, 1, 2, 3, 4, 4}, {0}, 1}};
    if (kind == 1)
      test_bus::replies = {{{0x52}, {4, 0}, 2},
                           {{0x93, 0x20}, {0x88, 1, 2, 3, 0x88}, 5},
                           {{0x93, 0x70, 0x88, 1, 2, 3, 0x88}, {4}, 1},
                           {{0x95, 0x20}, {4, 5, 6, 7, 0}, 5},
                           {{0x95, 0x70, 4, 5, 6, 7, 0}, {0}, 1}};
    if (kind == 2) test_bus::replies = {{{0x52}, {4, 0}, 2}, {{0x93, 0x20}, {1, 2, 3, 4, 5}, 5}};
    f.dev.start_select();
    f.finish();
    assert(f.dev.ok() == (kind != 2));
    assert(test_bus::replies.empty());
    if (kind != 2) {
      assert(f.dev.uid_length() == (kind == 0 ? 4 : 7));
      for (int i = 0; i < f.dev.uid_length(); i++) assert(f.dev.uid()[i] == i + 1);
    }
  }
  for (uint32_t flag : {0x10000u, 0x20000u, 0x40000u}) {
    Fixture f;
    test_bus::scripted = true;
    test_bus::replies = {{{0x3A, 0xF0, 0xFF}, std::vector<uint8_t>(64), 64 | flag}};
    f.dev.start_response();
    f.finish();
    assert(!f.dev.ok() && test_bus::command != 0x0A);
  }
  // SRAM is consumed exactly once, and missing RF IRQ reaches a finite failure.
  {
    Fixture f;
    test_bus::scripted = true;
    std::vector<uint8_t> bytes(64);
    for (int i = 0; i < 64; i++) bytes[i] = i;
    test_bus::replies = {{{0x3A, 0xF0, 0xFF}, bytes, 64}};
    f.dev.start_response();
    f.finish();
    assert(f.dev.ok());
    for (int i = 0; i < 64; i++) assert(f.dev.data()[i] == i);
  }
  {
    Fixture f;
    test_bus::scripted = true;
    test_bus::replies = {{{0x30, 0xEC}, {}, 0, false}};
    f.dev.start_session();
    f.finish();
    assert(!f.dev.ok() && test_bus::now < 500);
  }
  std::cout << "Driver tests passed: cooperative BUSY/IRQ waits, SPI boundaries, reset, UID cascades and RF errors.\n";
}
