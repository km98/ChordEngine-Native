# VST3 host test plan — development only

**Classification: MANUAL, DEVELOPMENT-ONLY host test. Not a release test and not a
host-parity sign-off.**

**Safety gate:** the persistent `TESTING; DO NOT TOUCH` gate is **not** established.
**No DAW may be automated** — no scripting, no GUI automation, no scripted clicks in
FL Studio, Ableton Live, or any other host. Every step is hand-driven.

This is the VST3 counterpart of `SECOND_MAC_HOST_TEST.md` (AU/Logic). It runs on a
**clean host** — a machine/environment where no ChordEngine build is installed.

## Preconditions

- Test build: `Build/TestArtifacts/ChordEngine-Native-macOS-VST3-Test.zip`
  (verify SHA-256 against `Build/TestArtifacts/MANIFEST.txt` first).
- Identity: version 0.1.0, product **ChordEngine**, manufacturer **Music-Prod**,
  bundle id `com.musicprod.chordenginefl`, VST3 class category `Fx`, MIDI input/
  output enabled, not a synth.
- Preferred hosts: **FL Studio** or **Ableton Live** (either, on a clean machine or
  clean user account with no ChordEngine installed).
- A **downstream software instrument** in the host that can receive MIDI from the
  plug-in track.
- New, disposable project only. Never open or save an existing project.
- No changes to HISE projects, the approved FL VST3, installed plug-ins, SideChainer,
  or any installer/DMG.

## Installation (manual, by the tester)

Unzip the archive and place `ChordEngine.vst3` in the host's VST3 folder for the
second Mac (macOS default: `~/Library/Audio/Plug-Ins/VST3/`), then rescan in the
host. Nothing is installed by the build on this development Mac.

## Test sequence (VST3)

1. **Plugin appears as `ChordEngine`** in the host's plug-in list, and its
   manufacturer/branding shows **Music-Prod**. Record the exact menu path shown.
2. **Create a new project.** Load ChordEngine on a plug-in track, then ensure MIDI
   from that track reaches a **downstream software instrument**.
3. **Generic host routing (no FL-only assumptions):**
   - Preferred: the host's normal "MIDI effect on a MIDI/instrument track" pattern.
   - In Ableton: place ChordEngine on a MIDI track **before** the instrument on the
     same track (device order: ChordEngine → Instrument), or route its MIDI output
     to a second track's input — whichever the host supports natively.
   - In FL Studio: use the standard "insert on the generator's channel/patcher as a
     MIDI effect" flow **only** if that is the host's documented pattern for MIDI
     FX; do not assume FL-specific event buses, port numbers, or wrapper behavior.
   - Whatever the host, record which routing was used — routing is part of the test
     result, not an assumption.
4. **MIDI enters**: play a valid trigger (MIDI 24–96; C4 = 60). Use a MIDI monitor
   (host's piano roll log, mixer meter, or a monitor plug-in) to observe events.
5. **Generated MIDI exits**: with default settings (C / Minor / Dreamy, WHOLE 0,
   Dynamic) trigger 60 should emit **51, 55, 58, 62** on the input channel, and the
   raw trigger note 60 should **not** pass through as a normal played note.
6. **Downstream instrument responds** to the generated chord (audible/visible
   response from the downstream instrument only).
7. **No unexpected audio** from ChordEngine itself: mute/solo checks — ChordEngine
   must produce no direct output.
8. **Musical behavior spot-checks** (record observed vs expected):
   - transpose: WHOLE / LOWEST / HIGHEST, steps −2…+2
   - scale change (e.g. Minor → Major) changes the chord tones
   - preset change (e.g. Dreamy → Cinematic = spread voicing, Ambient = open voicing)
   - velocity: Dynamic follows played velocity, Maximum = 127, Fixed = configured
     value (100 default) — **record which mode this build starts in**; the
     Dynamic-vs-Fixed default conflict is open in
     `Docs/DEFAULT_BEHAVIOR_DISCREPANCY.md` and must not be changed during testing
   - note release: chord note-offs follow trigger release
   - sustain: CC64 ≥64 holds, <64 releases
   - panic: CC120 and CC123 release all generated notes
9. **Out-of-range triggers**: notes 23 and below, 97 and above → no chord, raw note
   suppressed (record what the host actually shows).
10. **Record exactly what happens** per step: input, expected, observed (pitches,
    velocity, channel, ordering, timing feel), pass/fail/unknown.

## Known reference expectations (source-derived, not host-observed)

Same table as in `SECOND_MAC_HOST_TEST.md`, sourced from
`Tests/FLReferenceVectors.json` (`hostObserved: false`). Any mismatch is a recorded
finding, not a hotfix. Host-level behavior — actual routing, timestamps, host
filtering, state save/reload — is explicitly **unknown** until observed.

## Recording template

```
Date / tester / machine / macOS version / host + version:
Archive SHA-256 verified: yes/no (VST3: d6738153...)
Menu path where ChordEngine appears: ...
Routing used: ...
Step 4 MIDI in: ...
Step 5 output pitches/velocity/channel vs expected (51,55,58,62): ...
Step 6 downstream response: ...
Step 7 ChordEngine silent: yes/no
Step 8 transpose/scale/preset/velocity/release/sustain/panic: ...
Step 9 out-of-range triggers: ...
Unexpected behavior / errors / crashes: ...
Overall: pass / fail / blocked, unknowns listed
```

## Out of scope

No licensing/trial backend, no release packaging, no distribution signing, no
notarization, no publishing, no DAW automation, no changes to the native default
velocity behavior, and no edits to any HISE/reference artifact. The GUI is a native
JUCE implementation; Logic GUI testing must use a new disposable project only.
