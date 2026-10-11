# ESPHome quality status — 1.0, 2026-10-11

Local tests and a successful build do not constitute upstream ESPHome acceptance.
See TESTING.md for the checks actually run and README.md for installation.

## Implemented

- [x] Standard ESPHome SPI registration and hardware-bus selection through YAML.
- [x] Dedicated-bus validation; reject conflicting devices rather than holding a
  bus lock across scheduler iterations.
- [x] Cooperative BUSY/IRQ waits, reset, RF control, selection, EEPROM handling
  and complete mailbox exchange. At most one SPI frame per loop invocation.
- [x] Per-instance buffers and bounded command plans; no static receive buffer.
- [x] High-frequency loop request during work, released when idle or failed.
- [x] Full two-request scan, both UID lengths, DPC verification, timeout/fault
  recovery, CRC checks, invalidation and no-replay regression coverage.
- [x] C++ formatting of the changed driver/component and tests.
- [x] Preserved LGPL attribution; partial provenance review in PROVENANCE.md.

- [x] Missing-tag status and one actionable warning per failed scan; selection
  stage and IRQ timeout at DEBUG. The observed antenna displacement is context,
  not a software diagnosis.
- [x] One INFO recovery message after consecutive failures; uint32_t counter
  saturates at 4,294,967,295 instead of 255. Sensor invalidation threshold unchanged.
- [x] Bounded stack buffers for UID, raw hex and dynamic diagnostic statuses;
  removed unused string formatter. ESPHome text-sensor publication still uses
  the framework's string storage.
- [x] Python CONF_* keys and import/format alignment; explicit C++ member access
  in implementation files, reproducible formatting and local lint in CI.

## Remaining engineering work

- [ ] Measure on-device operation duration, API responsiveness and long-run
  reliability, including fault paths and power-loss recovery.
- [ ] Complete a controlled on-device DPC restoration test; a boot with DPC
  already disabled does not exercise the write path.
- [ ] Complete the broader upstream convention review (including header member
  access, naming, conditional entity compilation and upstream-specific lint).
- [ ] Add upstream test configurations/docs if preparing a contribution, then
  run the actual upstream lint/CI checks and request maintainer review.
- [ ] Complete the previously discussed source/protocol provenance review before
  public release; retain all original license notices. Driver notice checks are
  recorded in PROVENANCE.md; protocol knowledge sources remain unverified.

The short hardware SPI frame transfer uses ESPHome's synchronous API. Cooperative
waits do not constitute a hard real-time deadline for unrelated components.
Shared buses, software SPI, multiple readers and PN532 are outside this release.

## Hardware status

The user reports successful operation, including the RGB status indicator.
Quantified long-run reliability, maximum loop duration, fault recovery and
controlled DPC restoration still need measurement. Battery impact remains
unverified.

References:
- https://developers.esphome.io/contributing/code/
- https://developers.esphome.io/architecture/components/spi/
- https://developers.esphome.io/contributing/submitting-your-work/
