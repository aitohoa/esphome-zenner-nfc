// SPDX-License-Identifier: LGPL-2.1-or-later
// Modified for Zenner NFC on 2026-10-08; see NOTICE.md.
#pragma once
#include <cstdint>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "iuw_protocol.h"
#include "pn5180.h"
namespace esphome {
namespace zenner_nfc {
class ZennerNfc : public PollingComponent, public PN5180 {
 public:
  float get_setup_priority() const override { return setup_priority::DATA; }
  void on_shutdown() override {
    nfc_->abort();
    rst_->digital_write(false);
  }
  void setup() override;
  void update() override;
  void loop() override;
  void dump_config() override;
  void set_uid_sensor(text_sensor::TextSensor *s) { uid_sensor_ = s; }
  void set_status_sensor(text_sensor::TextSensor *s) { status_sensor_ = s; }
  void set_raw_response_sensor(text_sensor::TextSensor *s) { raw_response_sensor_ = s; }
  void set_read_success_sensor(binary_sensor::BinarySensor *s) { read_success_sensor_ = s; }
  void set_consecutive_errors_limit(uint8_t n) { consecutive_errors_limit_ = n; }
  void set_dpc_mode(uint8_t mode) { dpc_mode_ = mode; }
  void set_flow_unit(uint8_t value) { flow_unit_ = value; }
  void set_busy_timeout(uint8_t ms) {
    busy_timeout_ms_ = ms;
    nfc_->set_busy_timeout(ms);
  }
  void set_volume_sensor(sensor::Sensor *s) { volume_sensor_ = s; }
  void set_forward_volume_sensor(sensor::Sensor *s) { forward_volume_sensor_ = s; }
  void set_reverse_volume_sensor(sensor::Sensor *s) { reverse_volume_sensor_ = s; }
  void set_flow_sensor(sensor::Sensor *s) { flow_sensor_ = s; }
  void set_water_temperature_sensor(sensor::Sensor *s) { water_temperature_sensor_ = s; }
  void set_ambient_temperature_sensor(sensor::Sensor *s) { ambient_temperature_sensor_ = s; }

 protected:
  bool rf_active_{false};
  uint8_t dpc_mode_{0};  // 0: preserve, 1: disabled, 2: enabled
  bool ready_{false};
  uint8_t boot_step_{0}, desired_dpc_{0};
  bool operation_started_{false};
  HighFrequencyLoopRequester high_frequency_;
  void boot_loop_();
  bool operation_done_();
  void idle_();
  uint8_t pn5180_versions_[6]{}, pn5180_dpc_[3]{};
  bool pn5180_versions_valid_{false}, pn5180_dpc_valid_{false};

  uint8_t flow_unit_{0}, busy_timeout_ms_{50};
  uint32_t consecutive_errors_{0};
  uint8_t consecutive_errors_limit_{3};
  binary_sensor::BinarySensor *read_success_sensor_{nullptr};
  sensor::Sensor *volume_sensor_{nullptr};
  sensor::Sensor *forward_volume_sensor_{nullptr};
  sensor::Sensor *reverse_volume_sensor_{nullptr};
  sensor::Sensor *flow_sensor_{nullptr};
  sensor::Sensor *water_temperature_sensor_{nullptr};
  sensor::Sensor *ambient_temperature_sensor_{nullptr};
  void invalidate_sensors_();
  void publish_current_(const iuw::Current &v);
  enum class ScanState : uint8_t {
    IDLE,
    INIT_RF,
    SETUP_RF,
    SELECT_TAG,
    WAIT_TX_GATE,
    SEND_REQUEST,
    WAIT_RESPONSE,
    READ_RESPONSE,
    RETRY_RF,
    STOP_RF,
    RECOVER
  };
  ScanState scan_state_{ScanState::IDLE};
  uint32_t next_action_{0}, scan_started_{0}, gate_started_{0}, sent_at_{0}, last_gate_log_{0};
  uint8_t selection_attempt_{0};
  uint8_t request_[6]{}, active_fc_{1}, last_ns_{0}, last_nc_{0};
  uint16_t seed_{0xFFFF};
  bool have_session_{false}, seen_i2c_ready_{false};
  void set_state_(ScanState state, uint32_t delay_ms = 0);
  void finish_scan_(const char *reason, bool success = false);
  void status_(const char *value);
  void log_session_(const char *label, const uint8_t *regs);
  PN5180 *nfc_{this};
  text_sensor::TextSensor *uid_sensor_{nullptr}, *status_sensor_{nullptr}, *raw_response_sensor_{nullptr};
};
}  // namespace zenner_nfc
}  // namespace esphome
