// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
namespace esphome { namespace sensor { class Sensor { public: float state=0; void publish_state(float v){state=v;} }; } }
