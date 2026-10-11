// SPDX-License-Identifier: LGPL-2.1-or-later
// Cooperative adaptation of the PN5180 work of Andreas Trappmann (2018)
// and Mark Hermann (2025). See NOTICE.md and LICENSE.
// Copyright (c) 2018 by Andreas Trappmann. All rights reserved.
// Copyright (c) 2025 by Mark Hermann. All rights reserved.
//
// This file is part of the Zenner NFC component. See NOTICE.md for upstream attribution.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// Lesser General Public License for more details.
//
#include "pn5180.h"

#include <cstring>

#include "esphome/core/log.h"

namespace esphome {
namespace zenner_nfc {
static const char *const TAG = "pn5180";
void PN5180::begin() {
  this->nss_->setup();
  this->nss_->digital_write(true);
  this->busy_->setup();
  this->rst_->setup();
  this->rst_->digital_write(true);
  this->spi_setup();
}
void PN5180::abort() {
  this->nss_->digital_write(true);
  this->host_ = Host::IDLE;
  this->active_ = false;
  this->ok_ = false;
}
void PN5180::fail_(const char *error, bool transport) {
  abort();
  this->error_ = error;
  this->transport_failed_ |= transport;
  ESP_LOGD(TAG, "%s", error);
}
void PN5180::start_() {
  this->count_ = this->index_ = 0;
  this->action_started_ = this->irq_reading_ = this->selecting_ = false;
  this->ok_ = false;
  this->error_ = "none";
  this->active_ = true;
  this->host_ = Host::IDLE;
}
void PN5180::command_(std::initializer_list<uint8_t> bytes, uint8_t receive) {
  if (this->count_ >= 40 || bytes.size() > 10 || receive > sizeof(this->rx_)) {
    this->fail_("invalid command plan");
    return;
  }
  auto &a = this->actions_[this->count_++];
  a = Action{};
  a.length = bytes.size();
  a.receive = receive;
  uint8_t i = 0;
  for (uint8_t b : bytes) a.bytes[i++] = b;
}
void PN5180::reg_(uint8_t op, uint8_t address, uint32_t value) {
  this->command_({op, address, uint8_t(value), uint8_t(value >> 8), uint8_t(value >> 16), uint8_t(value >> 24)});
}
void PN5180::action_(Kind kind, uint32_t value, uint8_t receive) {
  if (this->count_ >= 40) {
    this->fail_("command plan overflow");
    return;
  }
  auto &a = this->actions_[this->count_++];
  a = Action{};
  a.kind = kind;
  a.value = value;
  a.receive = receive;
}
uint32_t PN5180::word_() const {
  return uint32_t(this->rx_[0]) | (uint32_t(this->rx_[1]) << 8) | (uint32_t(this->rx_[2]) << 16) |
         (uint32_t(this->rx_[3]) << 24);
}
void PN5180::host_start_(const uint8_t *bytes, uint8_t length, uint8_t receive) {
  memcpy(this->host_tx_, bytes, length);
  this->host_length_ = length;
  this->host_receive_ = receive;
  this->host_ = Host::WAIT_LOW;
  this->phase_at_ = millis();
}
bool PN5180::host_tick_() {
  const uint32_t elapsed = millis() - this->phase_at_;
  switch (this->host_) {
    case Host::IDLE:
      return true;
    case Host::WAIT_LOW:
      if (this->busy_->digital_read()) {
        if (elapsed >= this->busy_timeout_) this->fail_("SPI/BUSY before command: expected LOW", true);
        return false;
      }
      this->nss_->digital_write(false);
      this->phase_at_ = millis();
      this->host_ = Host::COMMAND_SETUP;
      return false;
    case Host::COMMAND_SETUP:
      if (elapsed < 2) return false;
      this->enable();
      this->write_array(this->host_tx_, this->host_length_);
      this->disable();
      this->phase_at_ = millis();
      this->host_ = Host::COMMAND_HIGH;
      return false;
    case Host::COMMAND_HIGH:
      if (!this->busy_->digital_read()) {
        if (elapsed >= this->busy_timeout_) this->fail_("SPI/BUSY command accepted: expected HIGH", true);
        return false;
      }
      this->nss_->digital_write(true);
      this->phase_at_ = millis();
      this->host_ = Host::COMMAND_LOW;
      return false;
    case Host::COMMAND_LOW:
      if (elapsed < 1) return false;
      if (this->busy_->digital_read()) {
        if (elapsed >= this->busy_timeout_) this->fail_("SPI/BUSY command complete: expected LOW", true);
        return false;
      }
      if (this->host_receive_ == 0) {
        this->host_ = Host::IDLE;
        return true;
      }
      this->nss_->digital_write(false);
      this->phase_at_ = millis();
      this->host_ = Host::RESPONSE_SETUP;
      return false;
    case Host::RESPONSE_SETUP:
      if (elapsed < 2) return false;
      memset(this->rx_, 0xFF, this->host_receive_);
      this->enable();
      this->transfer_array(this->rx_, this->host_receive_);
      this->disable();
      this->phase_at_ = millis();
      this->host_ = Host::RESPONSE_HIGH;
      return false;
    case Host::RESPONSE_HIGH:
      if (!this->busy_->digital_read()) {
        if (elapsed >= this->busy_timeout_) this->fail_("SPI/BUSY response transferred: expected HIGH", true);
        return false;
      }
      this->nss_->digital_write(true);
      this->phase_at_ = millis();
      this->host_ = Host::RESPONSE_LOW;
      return false;
    case Host::RESPONSE_LOW:
      if (elapsed < 1) return false;
      if (this->busy_->digital_read()) {
        if (elapsed >= this->busy_timeout_) this->fail_("SPI/BUSY response complete: expected LOW", true);
        return false;
      }
      this->host_ = Host::IDLE;
      return true;
  }
  return false;
}
void PN5180::next_() {
  this->index_++;
  this->action_started_ = this->irq_reading_ = false;
}
void PN5180::tick() {
  if (!this->active_) return;
  if (this->transport_failed_) {
    this->fail_("SPI transport requires reset");
    return;
  }
  if (this->index_ == this->count_) {
    if (this->selecting_)
      this->selection_next_();
    else {
      this->active_ = false;
      this->ok_ = true;
    }
    return;
  }
  const auto &a = this->actions_[this->index_];
  if (!this->action_started_) {
    this->action_started_ = true;
    this->action_at_ = millis();
  }
  switch (a.kind) {
    case Kind::COMMAND:
      if (this->host_ == Host::IDLE) this->host_start_(a.bytes, a.length, a.receive);
      if (this->host_tick_()) this->next_();
      return;
    case Kind::PAUSE:
      if (millis() - this->action_at_ >= a.value) this->next_();
      return;
    case Kind::RESET_LOW:
      this->nss_->digital_write(true);
      this->rst_->digital_write(false);
      this->next_();
      return;
    case Kind::RESET_HIGH:
      this->rst_->digital_write(true);
      this->next_();
      return;
    case Kind::IRQ:
      if (!this->irq_reading_) {
        const uint8_t cmd[] = {0x04, 0x02};
        this->host_start_(cmd, sizeof(cmd), 4);
        this->irq_reading_ = true;
      }
      if (!this->host_tick_()) return;
      this->irq_reading_ = false;
      // An already asserted IRQ is valid even if polling was delayed by another component.
      if (this->word_() & a.value)
        this->next_();
      else if (millis() - this->action_at_ >= a.receive)
        this->fail_("IRQ response timeout");
      return;
    case Kind::RF_STATE:
      if (((this->word_() >> 24) & 7) != 1)
        this->fail_("transceiver not in WaitTransmit");
      else
        this->next_();
      return;
    case Kind::RX_SHAPE: {
      const uint32_t rx = this->word_();
      if (rx & 0x70000) {
        ESP_LOGW(TAG, "RF error RX=%08lX", static_cast<unsigned long>(rx));
        this->fail_((rx & 0x40000) ? "RF collision" : ((rx & 0x20000) ? "RF protocol error" : "RF integrity error"));
      } else if ((rx & 0x1FF) != a.value || ((rx >> 13) & 7) != a.receive) {
        this->fail_("RF response length/bits mismatch");
      } else
        this->next_();
      return;
    }
    case Kind::ACK:
      if ((this->rx_[0] & 15) != 10)
        this->fail_("A2 NAK; no replay");
      else
        this->next_();
      return;
  }
}
void PN5180::start_reset() {
  abort();
  this->transport_failed_ = false;
  this->start_();
  this->action_(Kind::RESET_LOW);
  this->action_(Kind::PAUSE, 1);
  this->action_(Kind::RESET_HIGH);
  this->action_(Kind::PAUSE, 3);
  this->action_(Kind::IRQ, 4, 10);
  this->reg_(0, 3, 0xFFFFFFFF);
}
void PN5180::start_rf(bool on) {
  this->start_();
  if (on) this->command_({0x11, 0, 0x80});
  this->command_({uint8_t(on ? 0x16 : 0x17), 0});
  this->action_(Kind::IRQ, on ? 0x200 : 0x100, 10);
  this->reg_(0, 3, on ? 0x200 : 0x100);
  if (on) this->action_(Kind::PAUSE, 5);
}
void PN5180::start_eeprom_read(uint8_t address, uint8_t length) {
  this->start_();
  if (length == 0 || length > 64 || unsigned(address) + length > 255) {
    this->fail_("invalid EEPROM range");
    return;
  }
  this->command_({7, address, length}, length);
}
void PN5180::start_dpc_write(uint8_t value) {
  this->start_();
  this->command_({6, 0x59, value});
}
void PN5180::exchange_(const uint8_t *bytes, uint8_t length, uint8_t expected, bool short_ack, uint8_t valid_bits) {
  this->reg_(0, 3, 0xFFFFFFFF);
  this->reg_(2, 0, 0xFFFFFFF8);
  this->reg_(1, 0, 3);
  this->command_({4, 0x1D}, 4);
  this->action_(Kind::RF_STATE);
  if (length > 8) {
    this->fail_("RF command too long");
    return;
  }
  this->command_({9, valid_bits});
  auto &send = this->actions_[this->count_ - 1];
  memcpy(send.bytes + 2, bytes, length);
  send.length += length;
  this->action_(Kind::IRQ, 1, 20);
  this->command_({4, 0x13}, 4);
  this->action_(Kind::RX_SHAPE, expected, short_ack ? 4 : 0);
  this->command_({0x0A, 0}, expected);
  if (short_ack) this->action_(Kind::ACK);
}
void PN5180::start_select() {
  this->start_();
  this->selecting_ = true;
  this->selection_stage_ = 0;
  this->uid_length_ = 0;
  this->selection_next_();
}
void PN5180::selection_next_() {
  this->count_ = this->index_ = 0;
  this->action_started_ = this->irq_reading_ = false;
  switch (this->selection_stage_++) {
    case 0:
      this->command_({0x11, 0, 0x80});
      this->reg_(2, 0, 0xFFFFFFBF);
      this->reg_(2, 0x12, 0xFFFFFFFE);
      this->reg_(2, 0x19, 0xFFFFFFFE);
      return;
    case 1: {
      const uint8_t cmd[] = {0x52};
      this->exchange_(cmd, 1, 2, false, 7);
      return;
    }
    case 2: {
      const uint8_t cmd[] = {0x93, 0x20};
      this->exchange_(cmd, 2, 5);
      return;
    }
    case 3:
    case 5: {
      if ((this->rx_[0] ^ this->rx_[1] ^ this->rx_[2] ^ this->rx_[3]) != this->rx_[4]) {
        this->fail_("UID BCC mismatch");
        return;
      }
      this->select_cmd_[0] = this->selection_stage_ == 4 ? 0x93 : 0x95;
      this->select_cmd_[1] = 0x70;
      memcpy(this->select_cmd_ + 2, this->rx_, 5);
      this->reg_(1, 0x12, 1);
      this->reg_(1, 0x19, 1);
      this->exchange_(this->select_cmd_, 7, 1);
      return;
    }
    case 4:
      if (this->rx_[0] & 4) {
        if (this->select_cmd_[2] != 0x88) {
          this->fail_("invalid UID cascade tag");
          return;
        }
        memcpy(this->uid_, this->select_cmd_ + 3, 3);
        this->reg_(2, 0x12, 0xFFFFFFFE);
        this->reg_(2, 0x19, 0xFFFFFFFE);
        const uint8_t cmd[] = {0x95, 0x20};
        this->exchange_(cmd, 2, 5);
        return;
      }
      memcpy(this->uid_, this->select_cmd_ + 2, 4);
      this->uid_length_ = 4;
      break;
    case 6:
      if (this->rx_[0] & 4) {
        this->fail_("unsupported UID cascade");
        return;
      }
      memcpy(this->uid_ + 3, this->select_cmd_ + 2, 4);
      this->uid_length_ = 7;
      break;
    default:
      this->fail_("invalid selection state");
      return;
  }
  this->active_ = false;
  this->ok_ = true;
  this->selecting_ = false;
}
void PN5180::start_session() {
  this->start_();
  const uint8_t cmd[] = {0x30, 0xEC};
  this->exchange_(cmd, 2, 16);
}
void PN5180::start_response() {
  this->start_();
  const uint8_t cmd[] = {0x3A, 0xF0, 0xFF};
  this->exchange_(cmd, 3, 64);
}
void PN5180::start_request(const uint8_t request[6]) {
  this->start_();
  uint8_t physical[8]{};
  for (uint8_t i = 0; i < 6; i++) physical[7 - i] = request[i];
  for (uint8_t page = 0; page < 2; page++) {
    this->reg_(1, 0x19, 1);
    this->reg_(2, 0x12, 0xFFFFFFFE);
    const uint8_t *p = physical + page * 4;
    const uint8_t cmd[] = {0xA2, uint8_t(0xFE + page), p[0], p[1], p[2], p[3]};
    this->exchange_(cmd, 6, 1, true);
    this->reg_(1, 0x12, 1);
    this->reg_(0, 3, 0xFFFFFFFF);
  }
}
}  // namespace zenner_nfc
}  // namespace esphome
