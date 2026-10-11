# Source and protocol provenance review — 2026-10-11

## Confirmed in this review

The PN5180 driver derives from Andreas Trappmann's Arduino library and Mark
Hermann's ESPHome adaptation. Their source headers explicitly grant
LGPL-2.1-or-later and identify the respective 2018 and 2025 copyrights:

- https://github.com/ATrappmann/PN5180-Library/blob/master/PN5180.h
- https://github.com/dbmaxpayne/esphome_qalcosonicnfc/blob/main/components/qalcosonicnfc/PN5180.h

The local pn5180.cpp and pn5180.h retain both copyright notices, license and
warranty notices. NOTICE.md credits both projects; LICENSE contains LGPL 2.1
and COPYING contains the accompanying GPL v2. No original notice was removed
by this change. These upstream links refer to moving branches, not a verified
record of the historical revisions used to create this component.

## Still required before public release

- Identify and record the exact upstream revisions originally used. The local
  history contains one initial commit and does not establish that chain.
- Establish the origin and redistribution terms of the Zenner identification,
  current-data layouts, CRC seed derivation and error codes. The implementation
  and synthetic tests alone do not establish where that knowledge originated.
- Record any protocol documents, captures or other references supplied during
  development, including their authorship and any applicable restrictions.

This review verifies the visible driver notices. It does not resolve the
outstanding protocol provenance questions or certify release readiness.
