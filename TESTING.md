# Zenner NFC 1.0 validation — 2026-10-11

## Local validation

- ESPHome 2026.9.1 configuration/code-generation suite: 23 cases passed, including
  minimal optional-entity configuration, flow units, DPC modes, encrypted API/OTA
  example and rejection of missing MISO/MOSI, shared/software SPI, wrong mode/speed,
  release-device mode and old-style pin configuration.
- Full ESP32-C6 / ESP-IDF 5.5.5 firmware build passed: image size 858,170 bytes with the RGB status indicator.
- Protocol, production asynchronous driver and production component tests passed
  with AddressSanitizer/UndefinedBehaviorSanitizer. LeakSanitizer was disabled for
  the local container's process-inspection limitation.
- The compiled component source inputs were checked against the package.

## Test coverage

Protocol tests retain independent CRC vectors, corruption/truncation checks,
identification layouts, optional temperatures, unit conversion and invalid values.

Driver tests cover all five BUSY wait phases, NSS cleanup, transport-failure
latching and reset recovery; delayed readiness and millis wrap; complete four-
and seven-byte UID selection, UID BCC, RF collision/protocol/integrity rejection,
SRAM response data and missing RX IRQ.

Every simulated tick asserts that no delay advances the clock, at most one SPI
frame is transferred and the SPI acquisition is released before returning. SPI
transfers require acquisition and asserted NSS. These checks validate cooperative
control flow; they do not measure real ESP32 scheduler or hardware latency.

Component tests exercise startup, DPC preserve/disable/enable, idempotent matching
settings, failed/ignored writes, failed reads and failed post-reset verification.
They run a full synthetic identification/current-data scan through the production
SPI/RF state machines and validate published data. Additional cases cover CRC
rejection, bounded selection retries with RF-off pause, partial A2 failure without
replay, nonempty mailbox, SPI recovery, sensor invalidation/recovery, RF shutdown
failure and expiration of deadlines without request replay.

The simulation models PN5180 BUSY staying high until NSS rises. It does not model
analog RF behavior, antenna tuning, meter processing delays or physical EEPROM.
The user reports successful on-device operation. Quantified long-run success
rate and maximum loop duration have not yet been measured.

## Reproduce

```sh
python -m pip install -r requirements-dev.txt
sh tests/run_cpp_tests.sh
python tests/config_test.py
esphome compile tests/compile.yaml
```

In this container only, run C++ tests with `ASAN_OPTIONS=detect_leaks=0`.
The compile-only YAML contains placeholder network credentials and example pins;
its generated firmware is not a deployment image for your installation.

## Hardware acceptance

1. Preserve the working log and YAML, then use the SPI configuration in README.md. Keep the same pins,
   antenna position, DPC setting and 60-second interval.
2. Capture the entire startup. Confirm version 1.0, DPC 0x72/disabled and a ready status.
   No EEPROM write is expected if it was already disabled.
3. Check volume, nonzero flow during water use and both temperature entities.
4. Capture a comparable long run. Count final statuses, internal retries, restarts,
   long-operation warnings and gaps. Check API/OTA responsiveness during reads.
5. Verify power-loss recovery. Separately, when appropriate, verify a controlled
   DPC enable/disable cycle; a boot with matching EEPROM does not test writing.
6. If regression occurs, restore the previous working component directory and YAML. Report the
   complete boot and surrounding DEBUG lines rather than only a final error.

Upstream ESPHome CI/lint acceptance and extended hardware validation remain open. See
QUALITY.md; no claim of full upstream compliance is made by these local results.

## Follow-up validation — 2026-10-11

- Sanitized C++ protocol, driver and component suites passed with
  `ASAN_OPTIONS=detect_leaks=0`.
- Regression coverage now includes 352 complete missing-tag scans, exactly one
  warning per failed scan, stage/IRQ diagnostics at DEBUG, one recovery INFO
  message, counter reset and uint32_t saturation without wraparound.
- ESPHome 2026.9.1: all 23 configuration/code-generation cases passed.
- Python Ruff E/F/I lint and formatting, and clang-format 18 formatting are
  reproducible with `requirements-quality.txt` and the repository CI commands.
- Full ESP32-C6 / ESP-IDF 5.5.5 build passed; hardware acceptance remains open.

The build-only configuration and its binary are not intended for deployment.

## RGB status indicator

The ESP32-C6-DevKitC-1 example and compile-only configuration include the GPIO8
WS2812 indicator at 10% brightness. The component test checks that starting a
scan publishes `reading`; the configuration suite validates the LED automation.
The full ESP32-C6 / ESP-IDF 5.5.5 firmware build passed with the automation
(image size 858,170 bytes).
On hardware, verify yellow during startup/reading, green after a successful read
and red after a failed scan; restore the antenna and verify green returns after
the next successful read. The user has confirmed that the RGB status indication works on the device.
