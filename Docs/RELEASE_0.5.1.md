# ChordEngine 0.5.1 — licensing in the processor

**Status: built from `main` (`0e9a31a`), signed, notarized and submitted for approval
(release `c86688b6-1abd-4004-9416-7f7404daada4`, `ready_for_approval`). Not published by this
session.**

## Why

Owner decision (2026-10-22): ChordEngine is included for every Music-Prod+ member
(active or lifetime), and a member whose Music-Prod+ ends must fall back to the
demo/trial. In 0.5.0 the licence check and the 30-minute trial clock lived in the
editor, which left three gaps:

1. Never opening the editor meant unlimited chords: the gate started open and
   nothing closed it.
2. Reopening the editor (or reloading the plug-in) restarted the trial: the remaining
   budget was not persisted.
3. The entitlement was re-checked only while the editor was open, so a lapsed
   membership was never noticed by a plug-in whose window stayed closed.

## What changed

- `Source/Licensing/LicensingRuntime.{h,cpp}` (new) is owned by
  `ChordEngineAudioProcessor`. At construction it restores the token, the last verified
  entitlement (with its 14-day subscription / 7-day licence offline window) and the
  remaining trial budget from `auth.json`, and publishes the gate before the first
  audio block. The gate now starts **closed** until that decision is published.
- A 10 Hz message-thread timer in the runtime advances the trial clock (after the first
  generated chord, only while unlicensed, at most one second per step so a sleeping
  computer does not burn the trial) and drives the account state machine, which
  re-verifies every 15 minutes on the auth service's background worker.
- `prepareToPlay` and opening the editor request an immediate re-check (rate limited to
  one per minute). Opening the editor also reloads `auth.json` when another instance
  signed in.
- A re-check answer with `plugin_entitled: false` drops the licence at once: the trial
  takes over, and the gate closes when the trial is used up (immediately if it already
  was).
- `auth.json` now also stores `trial_remaining_ms` and `trial_expired`. Saves are
  merged with what is on disk (the smaller budget wins), so several instances share one
  budget and no instance can hand back time another used. Older files without the
  fields keep a full budget.
- The editor no longer owns an auth service or a boundary: it displays the processor's
  `LicensingBoundary` and forwards its buttons.
- Test builds (`CHORDENGINE_ISOLATED_LICENSING`) give every processor a private
  temporary `auth.json` and never start the timer, so no test reads the developer's
  account or touches the network.
- Version 0.5.1 (`CHORDENGINE_VERSION`, `PackageMacDMG.sh` default).

Not changed: the server contract, the access decision (`plugin_entitled` only), the
offline windows, the product's `licensed` access model. ChordEngine shows no AI model
names, so the CoProducer naming does not apply.

## Tests

- New `ChordEngineLicensingRuntimeTests` (8 cases): gate decided at construction with no
  editor; trial waits for the first chord; trial persisted across reloads and an expired
  trial closes the gate at construction; stalled-timer clamp; member licensed from the
  saved answer, 14/7-day windows; re-check on request with one-minute rate limit;
  lapsed membership relocks to the trial and then closes, or closes at once with a used
  trial; two instances share one budget; editor open picks up another instance's sign-in.
- `ProcessorIntegrationTests` case 23: a processor with no editor generates trial chords,
  starts the clock from its own chord, closes the gate when the trial runs out and then
  generates no chord.
- CTest 14/14 in `Build/macOS-JUCE9`.

## Signed build and release record (2026-10-22)

Built in `/tmp/ChordEngine-0.5.1-RC-20261022a` with the documented configure; CTest 14/14;
`PackageMacDMG.sh --version 0.5.1` exit 0. PKG notarization `2f080fd1-d0fa-488d-881f-6d40de241bde`
and DMG notarization `99588ebe-646d-4e8f-b382-cdea70add5b6` Accepted, both stapled, Gatekeeper
"Notarized Developer ID". The PKG inside the mounted DMG equals the standalone PKG, its
PackageInfo says `com.musicprod.chordengine.pkg` 0.5.1, and all three payload bundles are 0.5.1.

| File | Bytes | SHA-256 |
| --- | --- | --- |
| `ChordEngine-0.5.1-macOS.pkg` (inside the DMG) | 26731510 | `e8f0356a7dabf5f1bbef17562b8dde79a288653a8d5d16244859180e526e9476` |
| `ChordEngine-0.5.1-macOS.dmg` | 27827524 | `d534e1584c29f0b38119364db18aef5f806d240cf930a9832fb66640f8761b31` |

Release record (publisher API, write scope; same shape as 0.5.0):

- Release `c86688b6-1abd-4004-9416-7f7404daada4`: product `chordengine`, 0.5.1, stable,
  build 2, not mandatory, minimum macOS 12.0. Status `ready_for_approval`.
- Artifact `fa224b01-17aa-442f-b97c-1956b909649f`: `dmg`, macos, universal; server SHA-256 and
  size equal the table.
- `installerMetadata`: pkg `ChordEngine-0.5.1-macOS.pkg` with the SHA-256 and size above,
  identifier `com.musicprod.chordengine.pkg`, version 0.5.1, install location `/`; codesign
  Developer ID Installer: Martin Kadziolka (3A4R5EKM7V), signed 2026-10-22T08:30:19Z;
  notarization accepted, stapled, submission `2f080fd1-…`; minimum macOS 12.0.
- At submission, 0.5.0 (`11d91ba3-…`, build 1) was already **published** (not by this
  session); publishing 0.5.1 supersedes it.
