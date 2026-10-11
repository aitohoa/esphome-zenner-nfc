// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <cstdio>
#include <utility>

#include "esphome/components/spi/spi.h"
namespace test_bus {
inline std::vector<std::pair<char, std::string>> leveled_logs;
template <class... T>
void log(char level, const char *, const char *fmt, T... args) {
  char text[512];
  std::snprintf(text, sizeof(text), fmt, args...);
  logs.emplace_back(text);
  leveled_logs.emplace_back(level, text);
}
}  // namespace test_bus
#define ESP_LOGD(...) test_bus::log('D', __VA_ARGS__)
#define ESP_LOGI(...) test_bus::log('I', __VA_ARGS__)
#define ESP_LOGW(...) test_bus::log('W', __VA_ARGS__)
#define ESP_LOGE(...) test_bus::log('E', __VA_ARGS__)
#define ESP_LOGCONFIG(...) test_bus::log('C', __VA_ARGS__)
