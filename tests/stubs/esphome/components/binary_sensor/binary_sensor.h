// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
namespace esphome { namespace binary_sensor { class BinarySensor { public: bool state=false; void publish_state(bool v){state=v;} }; } }
