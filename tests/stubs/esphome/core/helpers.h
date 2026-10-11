#pragma once
namespace esphome { class HighFrequencyLoopRequester { public: bool active=false; void start(){active=true;} void stop(){active=false;} }; }
