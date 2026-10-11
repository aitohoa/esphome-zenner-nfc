// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <string>
namespace esphome { namespace text_sensor { class TextSensor { public: std::string state; void publish_state(const std::string& v){state=v;} }; } }
