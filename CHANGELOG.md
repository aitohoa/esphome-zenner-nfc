# Changelog

## 1.0 — 2026-10-11

- Read Zenner IUW/S volume, forward/reverse totals, flow and temperatures into Home Assistant.
- Integrate PN5180 through a dedicated ESPHome hardware SPI bus.
- Use cooperative reader operations with bounded waits, checked responses and fault recovery.
- Support configurable DPC preservation, disabling and enabling with EEPROM readback verification.
- Report missing-tag failures, detailed DEBUG diagnostics and recovery with a 32-bit failure count.
- Provide GPIO8 RGB status indication for ESP32-C6-DevKitC-1: yellow while starting/reading,
  green on success and red on failure.
- Include protocol/driver/component regression tests, configuration validation and build/lint CI.
- Preserve original LGPL driver attribution and document remaining quality work.
