// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
namespace esphome {
namespace setup_priority { constexpr float DATA = 600.0f; }
class PollingComponent {
 public:
  virtual float get_setup_priority() const { return 0; }
  virtual void on_shutdown() {}
  virtual ~PollingComponent() = default;
  virtual void setup() {} virtual void update() {} virtual void loop() {} virtual void dump_config() {}
  bool warning=false, failed=false;
  bool is_failed() const { return failed; }
  void status_set_warning(){warning=true;} void status_clear_warning(){warning=false;}
  void mark_failed(const char*){failed=true;}
};
}
#define LOG_STR(x) x
#define LOG_UPDATE_INTERVAL(x) ((void)0)
