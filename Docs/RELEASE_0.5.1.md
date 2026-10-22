# ChordEngine 0.5.1 — licensing in the processor

**Status: built from `main`, signed, notarized and submitted for approval (see the end).
Not published by this session.**

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
