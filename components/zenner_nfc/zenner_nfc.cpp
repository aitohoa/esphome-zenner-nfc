// SPDX-License-Identifier: LGPL-2.1-or-later
// Modified for Zenner NFC on 2026-10-08; see NOTICE.md.
#include "zenner_nfc.h"

#include <cstdio>
#include <cstring>

#include "esphome/core/log.h"
#include "iuw_protocol.h"

namespace esphome {
namespace zenner_nfc {
// A 64-byte diagnostic payload needs at most 192 bytes including NUL.
static void format_hex(char *out, size_t capacity, const uint8_t *data, size_t length, char separator) {
  static const char HEX[] = "0123456789ABCDEF";
  size_t used = 0;
  for (size_t i = 0; i < length; i++) {
    if (used + (i ? 3 : 2) >= capacity) break;
    if (i) out[used++] = separator;
    out[used++] = HEX[data[i] >> 4];
    out[used++] = HEX[data[i] & 15];
  }
  if (capacity) out[used] = '\0';
}
static const char *const TAG = "zenner_nfc";
void ZennerNfc::setup() {
  if (this->read_success_sensor_) this->read_success_sensor_->publish_state(false);
  this->nfc_->begin();
  this->high_frequency_.start();
  this->status_("initializing PN5180");
  this->nfc_->start_reset();
}
void ZennerNfc::boot_loop_() {
  if (this->nfc_->active()) return;
  if (!this->nfc_->ok()) {
    ESP_LOGE(TAG, "PN5180 startup failed at step %u: %s", this->boot_step_, this->nfc_->error());
    this->status_("PN5180 startup/DPC configuration failed");
    this->nfc_->abort();
    this->rst_->digital_write(false);
    this->high_frequency_.stop();
    mark_failed(LOG_STR("PN5180 startup failed"));
    return;
  }
  switch (this->boot_step_) {
    case 0:
      this->boot_step_ = 1;
      this->nfc_->start_eeprom_read(0x10, 6);
      return;
    case 1:
      memcpy(this->pn5180_versions_, this->nfc_->data(), 6);
      this->pn5180_versions_valid_ = true;
      this->boot_step_ = 2;
      this->nfc_->start_eeprom_read(0x59, 3);
      return;
    case 2: {
      memcpy(this->pn5180_dpc_, this->nfc_->data(), 3);
      this->pn5180_dpc_valid_ = true;
      const uint8_t before = this->pn5180_dpc_[0];
      this->desired_dpc_ =
          this->dpc_mode_ == 1 ? uint8_t(before & 0xFE) : (this->dpc_mode_ == 2 ? uint8_t(before | 1) : before);
      if (before != this->desired_dpc_) {
        this->pn5180_dpc_valid_ = false;
        ESP_LOGW(TAG, "PN5180 DPC EEPROM change: 0x%02X -> 0x%02X (persistent)", before, this->desired_dpc_);
        this->boot_step_ = 3;
        this->nfc_->start_dpc_write(this->desired_dpc_);
        return;
      }
      break;
    }
    case 3:
      this->boot_step_ = 4;
      this->nfc_->start_eeprom_read(0x59, 1);
      return;
    case 4:
    case 6:
      if (this->nfc_->data()[0] != this->desired_dpc_) {
        this->status_("PN5180 DPC verification failed");
        this->nfc_->abort();
        this->rst_->digital_write(false);
        this->high_frequency_.stop();
        mark_failed(LOG_STR("DPC readback mismatch; no retry"));
        return;
      }
      if (this->boot_step_ == 4) {
        this->boot_step_ = 5;
        this->nfc_->start_reset();
        return;
      }
      memcpy(this->pn5180_dpc_, this->nfc_->data(), 3);
      this->pn5180_dpc_valid_ = true;
      break;
    case 5:
      this->boot_step_ = 6;
      this->nfc_->start_eeprom_read(0x59, 3);
      return;
  }
  this->ready_ = true;
  ESP_LOGI(TAG, "PN5180 DPC verified: control=0x%02X enabled=%s", this->pn5180_dpc_[0],
           (this->pn5180_dpc_[0] & 1) ? "YES" : "NO");
  dump_config();
  this->status_("ready");
  this->idle_();
}
bool ZennerNfc::operation_done_() {
  if (this->nfc_->active()) return false;
  this->operation_started_ = false;
  return true;
}
void ZennerNfc::idle_() {
  this->scan_state_ = ScanState::IDLE;
  this->operation_started_ = false;
  this->rf_active_ = false;
  this->high_frequency_.stop();
}
void ZennerNfc::dump_config() {
  ESP_LOGCONFIG(TAG, "Zenner NFC 1.0: documented 01 -> 29, A2 transport");
  if (this->pn5180_versions_valid_) {
    ESP_LOGCONFIG(TAG, "PN5180 product version: %X.%X (raw %02X %02X)", this->pn5180_versions_[1],
                  this->pn5180_versions_[0], this->pn5180_versions_[0], this->pn5180_versions_[1]);
    ESP_LOGCONFIG(TAG, "PN5180 firmware version: %X.%X (raw %02X %02X)", this->pn5180_versions_[3],
                  this->pn5180_versions_[2], this->pn5180_versions_[2], this->pn5180_versions_[3]);
    ESP_LOGCONFIG(TAG, "PN5180 EEPROM version: %X.%X (raw %02X %02X)", this->pn5180_versions_[5],
                  this->pn5180_versions_[4], this->pn5180_versions_[4], this->pn5180_versions_[5]);
  } else {
    ESP_LOGCONFIG(TAG, "PN5180 versions: unavailable");
  }
  if (this->pn5180_dpc_valid_) {
    const uint8_t control = this->pn5180_dpc_[0];
    const unsigned ticks = unsigned(this->pn5180_dpc_[1]) | (unsigned(this->pn5180_dpc_[2]) << 8);
    ESP_LOGCONFIG(TAG, "PN5180 DPC EEPROM: control=0x%02X enabled=%s start_gear=%u step_size=%u", control,
                  (control & 1) ? "YES" : "NO", unsigned(control >> 4), unsigned((control >> 1) & 7));
    ESP_LOGCONFIG(TAG, "PN5180 DPC EEPROM: time_ticks=%u (%.2f us)", ticks, double(ticks) / 20.0);
  } else {
    ESP_LOGCONFIG(TAG, "PN5180 DPC EEPROM: unavailable");
  }
  ESP_LOGCONFIG(TAG, "PN5180 DPC requested: %s",
                this->dpc_mode_ == 0 ? "preserve" : (this->dpc_mode_ == 1 ? "disabled" : "enabled"));
  LOG_UPDATE_INTERVAL(this);
  ESP_LOGCONFIG(TAG, "SPI BUSY timeout: %u ms", this->busy_timeout_ms_);
}
void ZennerNfc::status_(const char *v) {
  if (this->status_sensor_) this->status_sensor_->publish_state(v);
}
void ZennerNfc::set_state_(ScanState s, uint32_t delay_ms) {
  this->operation_started_ = false;
  this->scan_state_ = s;
  this->next_action_ = millis() + delay_ms;
}
void ZennerNfc::finish_scan_(const char *reason, bool success) {
  ESP_LOGD(TAG, "END: %s (%u ms)", reason, unsigned(millis() - this->scan_started_));
  const bool needs_reset = this->nfc_->transport_failed() || this->nfc_->active();
  if (this->nfc_->transport_failed()) {
    char status[128];
    snprintf(status, sizeof(status), "PN5180 transport error: %s", this->nfc_->error());
    this->status_(status);
  }
  if (this->nfc_->active()) this->nfc_->abort();
  if (this->read_success_sensor_) this->read_success_sensor_->publish_state(success);
  if (success) {
    if (this->consecutive_errors_ != 0)
      ESP_LOGI(TAG, "NFC communication restored after %lu failed scans.",
               static_cast<unsigned long>(this->consecutive_errors_));
    this->consecutive_errors_ = 0;
    status_clear_warning();
  } else {
    if (this->consecutive_errors_ < UINT32_MAX) this->consecutive_errors_++;
    status_set_warning();
    if (strcmp(reason, "tag selection no response") == 0)
      ESP_LOGW(TAG, "Tag selection failed: no response. Check antenna position and distance.");
    else
      ESP_LOGW(TAG, "Read failed: %s (%lu consecutive failures)", reason,
               static_cast<unsigned long>(this->consecutive_errors_));
    if (this->consecutive_errors_ >= this->consecutive_errors_limit_) this->invalidate_sensors_();
  }
  this->set_state_(needs_reset ? ScanState::RECOVER : ScanState::STOP_RF);
}
void ZennerNfc::log_session_(const char *label, const uint8_t *r) {
  if (!this->have_session_ || r[0] != this->last_nc_ || r[6] != this->last_ns_ ||
      millis() - this->last_gate_log_ >= 1000) {
    ESP_LOGD(TAG, "FC=%02X +%u ms", this->active_fc_, unsigned(millis() - this->scan_started_));
    ESP_LOGD(TAG, "SESSION %s NC=%02X NS=%02X", label, r[0], r[6]);
    this->last_gate_log_ = millis();
  }
  if (this->active_fc_ && this->scan_state_ == ScanState::WAIT_RESPONSE && (r[6] & 0x10)) this->seen_i2c_ready_ = true;
  this->last_nc_ = r[0];
  this->last_ns_ = r[6];
  this->have_session_ = true;
}
void ZennerNfc::update() {
  if (is_failed() || !this->ready_) return;
  if (this->scan_state_ != ScanState::IDLE) {
    ESP_LOGW(TAG, "scan active; skipped");
    return;
  }
  this->high_frequency_.start();
  this->scan_started_ = millis();
  this->selection_attempt_ = 0;
  this->active_fc_ = 1;
  this->seed_ = 0xFFFF;
  this->have_session_ = false;
  this->seen_i2c_ready_ = false;
  iuw::request(this->request_, 1, 0xFFFF);
  this->status_("reading");
  ESP_LOGD(TAG, "========== NEW SCAN ==========");
  if (this->raw_response_sensor_) this->raw_response_sensor_->publish_state("");
  this->set_state_(ScanState::INIT_RF, 2000);
}
void ZennerNfc::loop() {
  if (is_failed()) return;
  this->nfc_->tick();
  if (!this->ready_) {
    this->boot_loop_();
    return;
  }
  if (this->scan_state_ == ScanState::IDLE || int32_t(millis() - this->next_action_) < 0) return;
  if (this->scan_state_ != ScanState::STOP_RF && this->scan_state_ != ScanState::RECOVER &&
      millis() - this->scan_started_ > 20000) {
    this->status_("scan timeout");
    this->finish_scan_("global timeout");
    return;
  }
  switch (this->scan_state_) {
    case ScanState::IDLE:
      return;
    case ScanState::INIT_RF: {
      if (!this->operation_started_) {
        this->nfc_->start_reset();
        this->operation_started_ = true;
        return;
      }
      if (!this->operation_done_()) return;
      if (!this->nfc_->ok()) {
        this->status_("PN5180 reset failed");
        this->finish_scan_("reset failed");
        return;
      }
      this->selection_attempt_++;
      this->set_state_(ScanState::SETUP_RF);
      return;
    }
    case ScanState::SETUP_RF: {
      if (!this->operation_started_) {
        this->rf_active_ = true;
        this->nfc_->start_rf(true);
        this->operation_started_ = true;
        return;
      }
      if (!this->operation_done_()) return;
      if (!this->nfc_->ok()) {
        this->status_("RF init failed");
        this->finish_scan_("RF init failed");
        return;
      }
      this->set_state_(ScanState::SELECT_TAG);
      return;
    }
    case ScanState::SELECT_TAG: {
      if (!this->operation_started_) {
        this->nfc_->start_select();
        this->operation_started_ = true;
        return;
      }
      if (!this->operation_done_()) return;
      if (!this->nfc_->ok()) {
        ESP_LOGD(TAG, "Selection attempt %u/2 failed at stage %u: %s", this->selection_attempt_,
                 this->nfc_->selection_stage(), this->nfc_->error());
        if (!this->nfc_->transport_failed() && this->selection_attempt_ < 2) {
          this->set_state_(ScanState::RETRY_RF);
          return;
        }
        const bool no_response =
            !this->nfc_->transport_failed() && strcmp(this->nfc_->error(), "IRQ response timeout") == 0;
        this->status_(no_response ? "NFC tag not responding" : this->nfc_->error());
        this->finish_scan_(no_response ? "tag selection no response" : "tag selection failed");
        return;
      }
      char u[21];
      format_hex(u, sizeof(u), this->nfc_->uid(), this->nfc_->uid_length(), ':');
      ESP_LOGD(TAG, "UID %s", u);
      if (this->uid_sensor_) this->uid_sensor_->publish_state(u);
      this->gate_started_ = millis();
      this->set_state_(ScanState::WAIT_TX_GATE);
      return;
    }
    case ScanState::RETRY_RF: {
      if (!this->operation_started_) {
        this->nfc_->start_rf(false);
        this->operation_started_ = true;
        return;
      }
      if (!this->operation_done_()) return;
      if (!this->nfc_->ok()) {
        this->status_("selection retry RF shutdown failed");
        this->finish_scan_("RF shutdown failed");
        return;
      }
      this->rf_active_ = false;
      this->set_state_(ScanState::INIT_RF, 100);
      return;
    }
    case ScanState::STOP_RF: {
      if (!this->operation_started_) {
        this->nfc_->start_rf(false);
        this->operation_started_ = true;
        return;
      }
      if (!this->operation_done_()) return;
      if (!this->nfc_->ok()) {
        this->set_state_(ScanState::RECOVER);
        return;
      }
      this->idle_();
      return;
    }
    case ScanState::RECOVER: {
      if (!this->operation_started_) {
        this->nfc_->start_reset();
        this->operation_started_ = true;
        return;
      }
      if (!this->operation_done_()) return;
      if (!this->nfc_->ok()) {
        ESP_LOGW(TAG, "Reader recovery failed; held in reset until next scan");
        this->nfc_->abort();
        this->rst_->digital_write(false);
      }
      this->idle_();
      return;
    }
    case ScanState::WAIT_TX_GATE: {
      if (millis() - this->gate_started_ >= 12000) {
        this->status_("TX gate timeout");
        this->finish_scan_("TX gate timeout");
        return;
      }
      if (!this->operation_started_) {
        this->nfc_->start_session();
        this->operation_started_ = true;
        return;
      }
      if (!this->operation_done_()) return;
      if (this->nfc_->transport_failed()) {
        this->finish_scan_("session SPI failure");
        return;
      }
      const uint8_t *r = this->nfc_->data();
      if (this->nfc_->ok()) {
        this->log_session_("WAIT TX", r);
        if ((r[6] & 0x18) != 0) {
          this->status_("mailbox not empty; retry with next scan");
          this->finish_scan_("pending mailbox data; no write");
          return;
        }
        bool ready = (r[6] & 0x01) && (r[6] & 0x20) && (r[0] & 0x01) && (r[0] & 0x40);
        if (ready) {
          this->set_state_(ScanState::SEND_REQUEST);
          return;
        }
      }
      this->next_action_ = millis() + 20;
      return;
    }
    case ScanState::SEND_REQUEST: {
      if (!this->operation_started_) {
        ESP_LOGD(TAG, "TX FC=%02X seed=%04X", this->active_fc_, this->seed_);
        this->nfc_->start_request(this->request_);
        this->operation_started_ = true;
        return;
      }
      if (!this->operation_done_()) return;
      if (!this->nfc_->ok()) {
        this->status_("request write failed");
        this->finish_scan_("A2 failed; no resend");
        return;
      }
      this->sent_at_ = millis();
      this->seen_i2c_ready_ = false;
      this->set_state_(ScanState::WAIT_RESPONSE);
      return;
    }
    case ScanState::WAIT_RESPONSE: {
      if (millis() - this->sent_at_ >= 2000) {
        ESP_LOGW(TAG, "I2C_READY observed=%u (a short transition may be missed)", this->seen_i2c_ready_);
        this->status_("response timeout");
        this->finish_scan_("RX timeout");
        return;
      }
      if (!this->operation_started_) {
        this->nfc_->start_session();
        this->operation_started_ = true;
        return;
      }
      if (!this->operation_done_()) return;
      if (this->nfc_->transport_failed()) {
        this->finish_scan_("session SPI failure");
        return;
      }
      const uint8_t *r = this->nfc_->data();
      if (this->nfc_->ok()) {
        this->log_session_("WAIT RX", r);
        if ((r[6] & 0x18) == 0x18) {
          this->status_("invalid mailbox state");
          this->finish_scan_("both READY flags");
          return;
        }
        bool ready = (r[6] & 0x01) && (r[6] & 0x08) && (r[6] & 0x20) && (r[0] & 0x40);
        // Observe early transitions but retain the app's minimum 100 ms before reading.
        if (ready && millis() - this->sent_at_ >= 100) {
          this->set_state_(ScanState::READ_RESPONSE);
          return;
        }
      }
      this->next_action_ = millis() + 10;
      return;
    }
    case ScanState::READ_RESPONSE: {
      if (!this->operation_started_) {
        this->nfc_->start_response();
        this->operation_started_ = true;
        return;
      }
      if (!this->operation_done_()) return;
      if (!this->nfc_->ok()) {
        this->status_("SRAM read failed");
        this->finish_scan_("FAST_READ failed");
        return;
      }
      uint8_t logical[64]{};
      const uint8_t *physical = this->nfc_->data();
      const size_t n = 64;
      for (size_t i = 0; i < n; i++) logical[i] = physical[63 - i];
      char hex[192];
      format_hex(hex, sizeof(hex), physical, 64, ' ');
      ESP_LOGD(TAG, "RX PHYSICAL64 %s", hex);
      format_hex(hex, sizeof(hex), logical, n, ' ');
      ESP_LOGD(TAG, "RX LOGICAL64 %s", hex);
      if (this->raw_response_sensor_) this->raw_response_sensor_->publish_state(hex);
      iuw::Frame f;
      if (!iuw::frame(logical, n, this->seed_, f)) {
        this->status_("invalid frame or CRC");
        this->finish_scan_("frame rejected");
        return;
      }
      ESP_LOGD(TAG, "FRAME OK len=%u cmd=%02X CRC=%s", unsigned(f.size), f.command,
               f.fallback_crc ? "FFFF fallback" : "request seed");
      if (f.command == 0xFE) {
        if (f.size == 8 && logical[2] >= 0x80 && logical[2] < 0xFF && logical[3] == 1) {
          uint16_t error = iuw::le16(logical + 4);
          bool matches = (logical[2] & 0x7F) == this->active_fc_;
          ESP_LOGW(TAG, "device error: request=%02X source=%02X matches=%u code=%04X %s", this->active_fc_,
                   logical[2] & 0x7F, matches, error, iuw::error_name(error));
          char status[128];
          snprintf(status, sizeof(status), "device error: %s", iuw::error_name(error));
          this->status_(status);
        } else {
          ESP_LOGW(TAG, "FE transport/unknown error; see raw");
          this->status_("transport/unknown error");
        }
        this->finish_scan_("error response; no follow-up");
        return;
      }
      if (f.command != this->active_fc_) {
        this->status_("unexpected response command");
        this->finish_scan_("command mismatch");
        return;
      }
      if (this->active_fc_ == 1) {
        iuw::Identity id;
        if (!iuw::identity(logical, f, id)) {
          this->status_("unsupported identification layout");
          this->finish_scan_("identity layout rejected");
          return;
        }
        this->seed_ = id.seed;
        ESP_LOGD(TAG, "IDENT OK format=%u firmware=%08lX meterID=%08lX seed=%04X", id.format,
                 (unsigned long)id.firmware, (unsigned long)id.meter_id, this->seed_);
        this->active_fc_ = 0x29;
        iuw::request(this->request_, this->active_fc_, this->seed_);
        this->have_session_ = false;
        this->gate_started_ = millis();
        this->set_state_(ScanState::WAIT_TX_GATE, 100);
        return;
      }
      iuw::Current v;
      if (!iuw::current(logical, f, v)) {
        this->status_("unsupported current-data layout");
        this->finish_scan_("current layout rejected");
        return;
      }
      if (v.has_temperatures) {
        ESP_LOGD(TAG, "optional fields: water_temperature=%.3f ambient_temperature_raw=%.3f scale_index=%u",
                 double(v.water_temperature), double(v.ambient_temperature), v.scale_index);
      }
      const char *unit = v.unit == 0 ? "m3" : "US gal";
      const char *flowunit = v.unit == 0 ? "m3/h" : "US gal/min";
      char summary[220];
      snprintf(summary, sizeof(summary), "OK: volume=%.6f %s; forward=%.6f; reverse=%.6f; flow=%.6f %s", v.volume, unit,
               v.forward, v.reverse, double(v.flow), flowunit);
      ESP_LOGD(TAG, "%s", summary);
      this->publish_current_(v);
      this->status_("ok");
      this->finish_scan_("current data OK", true);
      return;
    }
  }
}
void ZennerNfc::invalidate_sensors_() {
  for (auto *s : {this->volume_sensor_, this->forward_volume_sensor_, this->reverse_volume_sensor_, this->flow_sensor_,
                  this->water_temperature_sensor_, this->ambient_temperature_sensor_})
    if (s) s->publish_state(NAN);
}
void ZennerNfc::publish_current_(const iuw::Current &v) {
  const auto m = iuw::metric(v);
  if (this->volume_sensor_) this->volume_sensor_->publish_state(m.volume);
  if (this->forward_volume_sensor_) this->forward_volume_sensor_->publish_state(m.forward);
  if (this->reverse_volume_sensor_) this->reverse_volume_sensor_->publish_state(m.reverse);
  if (this->flow_sensor_) this->flow_sensor_->publish_state(iuw::flow_in_unit(m.flow, this->flow_unit_));
  if (this->water_temperature_sensor_)
    this->water_temperature_sensor_->publish_state(
        v.has_temperatures && std::isfinite(v.water_temperature) ? v.water_temperature : NAN);
  if (this->ambient_temperature_sensor_)
    this->ambient_temperature_sensor_->publish_state(
        v.has_temperatures && std::isfinite(v.ambient_temperature) ? v.ambient_temperature : NAN);
}
}  // namespace zenner_nfc
}  // namespace esphome
