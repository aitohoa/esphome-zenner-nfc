# License and attribution

Zenner NFC is distributed under the GNU Lesser General Public License,
version 2.1 or (at your option) any later version (`LGPL-2.1-or-later`).
See [PROVENANCE.md](PROVENANCE.md) for the partial source/protocol review.
See [LICENSE](LICENSE). [COPYING](COPYING) contains the accompanying GNU GPL v2
text referenced by the LGPL.

The PN5180 driver is derived from:

- [Andreas Trappmann's PN5180-Library](https://github.com/ATrappmann/PN5180-Library),
  Copyright (c) 2018 Andreas Trappmann.
- [Mark Hermann's esphome_qalcosonicnfc](https://github.com/dbmaxpayne/esphome_qalcosonicnfc),
  Copyright (c) 2025 Mark Hermann.

The upstream PN5180 source notices grant LGPL 2.1-or-later. Original copyright
and warranty notices are retained in the derived driver files. The historical
project name appears here to credit its authors; it is not a component name or
a configuration dependency of this project.

Changes made for Zenner NFC 1.0:

- Zenner IUW/S identification and current-data protocol implementation.
- NTAG pass-through mailbox transport and bounded polling state machine.
- Numeric ESPHome sensors, optional diagnostics, unit conversion and failure handling.
- PN5180 transport error propagation, bulk SPI reads, bounded BUSY waits and recovery.
- Cooperative PN5180 state machines, per-instance buffers and standard ESPHome SPI registration.
- DPC EEPROM configuration and SPI transaction cleanup with fault-injection tests.
- Cooperative NFC selection, bounded pre-request retries and RX error validation.
- Component and namespace rename; driver handshake cleanup and namespacing.
- English documentation, synthetic protocol tests and build configuration.

These changes are distributed under the same LGPL-2.1-or-later license.
The repository contains no vendor application binaries or decompiled source.
ESPHome and the firmware toolchain are separate projects with their own licenses.

This is an independent community project. ZENNER and other product names belong
to their respective owners; no affiliation or endorsement is implied.
