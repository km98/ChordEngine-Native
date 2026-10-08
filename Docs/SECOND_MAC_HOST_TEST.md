# Second-Mac Logic host test plan (AU) — development only

**Current host observation:** Martin manually confirmed on a clean second Mac that Logic recognizes Music-Prod → ChordEngine as a MIDI FX, accepts MIDI, generates the expected chord for a valid trigger, and routes generated MIDI to the downstream Classic Electric Piano/software instrument. This is recorded in [HostObservations.json](../Tests/HostObservations.json) as `hostObserved=true`. The 38 reference vectors remain source-derived (`hostObserved=false`). This does not verify the new GUI's appearance, controls, recorder drag/drop, or resizing.

**Classification: MANUAL, DEVELOPMENT-ONLY host test. Not a release test, not an
installer test, not a host-parity sign-off.**

**Safety gate:** this test is manual only. Do not automate Logic, FL Studio, or any
other DAW. Every step below is performed by a human, by hand.

## Preconditions

- Target: a **second Mac that has no ChordEngine installed** (no AU, no VST3, no
  HISE-built plugin anywhere in its plug-in folders).
- Test build: the current GUI milestone archive `Build/TestArtifacts/ChordEngine-Native-macOS-AU-GUI-Test.zip` (see `Build/TestArtifacts/MANIFEST.txt` for size and SHA-256 — verify before use).
- Development version 0.1.0, Music-Prod / ChordEngine, `aumi` MIDI Processor,
  MIDI in/out, no synth.
- Logic Pro (or Logic Pro trial) with a **downstream software instrument** available
  (any stock instrument such as Alchemy, Sampler, or the built-in Test Oscillator's
  neighbor — a normal instrument that makes sound from MIDI).
- This is development testing only: **do not open or save any existing Logic
  project.** Use a brand-new disposable project and leave it unsaved (or save only
  inside a scratch folder you delete afterwards).
- Do not modify `/Users/martin/Documents/HISE Projects/ChordEngine`,
  `ChordEngine-FL`, the approved FL VST3, the installed ChordEngine AU, SideChainer,
  or any installer/DMG — this test does not touch them at all.

## Safety rules for the session

1. Work only in a new, disposable Logic project.
2. Never open a production/existing Logic project, and never save over one.
3. Keep audio output at a safe level; the expectation is that **ChordEngine itself
   makes no sound** — if it does, stop and record the observation.
4. No automation of any kind. Hand-driven steps only.
5. Record observations while testing; do not "fix" anything during this session.

## Test sequence (AU)

1. **Install the AU test bundle** into `~/Library/Audio/Plug-Ins/Components/`
   by unzipping the archive and placing `ChordEngine.component` there.
   - Note: `~/Library` is the **home folder of the second Mac**, not this dev Mac.
   - If a `ChordEngine.component` already exists there, stop — the second Mac was
     supposed to be clean; record the situation and abort.
2. **Restart Logic**, or rescan Audio Units (Logic → Settings → Plug-in Manager →
   Reset & Rescan Selection).
3. **Open Logic Plug-in Manager.**
4. **Verify:** Vendor `Music-Prod` → Name `ChordEngine`, status Valid (if it shows as
   incompatible/failed, record the exact message and stop — do not force-rescan
   repeatedly or bypass Gatekeeper prompts).
5. **Create a NEW disposable Logic project.** Do not open any existing project.
6. **Create a Software Instrument track.**
7. **Insert ChordEngine in the MIDI FX position** of that track
   (MIDI FX slot → AU → Music-Prod → ChordEngine).
8. **Use a downstream software instrument** on the track (or on a second track fed
   by the first, per Logic's routing) so generated MIDI has somewhere to go.
   ChordEngine must not be the sound source.
9. **Play valid trigger notes** (MIDI 24–96, e.g. C3 = 48, C4 = 60, C5 = 72).
10. **Verify the raw trigger note is not passed as a normal played note** — i.e. the
    single key you pressed does not appear as a lone note in a MIDI monitor/downstream
    instrument; only chord output appears.
11. **Verify the expected chord MIDI reaches the downstream instrument.** For the
    factory default (C / Minor / Dreamy, WHOLE 0, Dynamic) a C4 (=60) trigger should
    produce the four-note chord **51, 55, 58, 62** (D#3, G3, A#3, C4) with the played
    velocity. Use Logic's MIDI monitor or the downstream instrument's display.
12. **Test the musical controls** and record what happens:
    - key changes (C → e.g. D)
    - scales (Minor → e.g. Major, Dorian)
    - chord presets (Dreamy → e.g. Basic, Cinematic, Ambient)
    - transpose targets/steps (WHOLE / LOWEST / HIGHEST, −2…+2)
    - velocity behavior — default is **Dynamic / 100** for this milestone; compare
      Dynamic vs Fixed vs Maximum without changing the default
    - note release (press, then release; chord note-offs must follow)
    - sustain (CC64 hold → release pedal → notes release)
    - panic (CC120 / CC123 → all generated notes release)
13. **Verify no unexpected audio comes directly from ChordEngine** — mute/bypass the
    downstream instrument and confirm ChordEngine alone is silent.
14. **Record exactly what happens**: per step, the input note(s)/velocity, expected
    output, observed output (pitches, velocity, channel, timing), and pass/fail/
    unknown. Photographs or screenshots of the MIDI monitor are welcome.
15. **Do not make any changes to the existing HISE implementation** and none to any
    reference artifact. This session only observes the native development build.

**After the session:** the manually copied test bundle may remain in the second Mac's
plug-in folder until a human removes it manually; nothing on this development Mac is
touched either way.

## Expected values to compare against (source-derived, not host-observed)

These come from the approved FL 0.4.0 embedded source via
`Tests/FLReferenceVectors.json` (`hostObserved: false`). The separate native AU routing
observation does not relabel them. A mismatch is a
**finding to record**, not something to fix during the test.

| Setting | Expected |
|---|---|
| Factory defaults | key C, Minor, Dreamy, transpose WHOLE 0, velocity Dynamic, fixed value 100 |
| Trigger range | MIDI 24–96 inclusive; outside → suppressed, no chord |
| C4 (60), default | 51, 55, 58, 62 on the input channel |
| Output register | base MIDI 48 + key + scale-degree offset (independent of trigger octave) |
| Note-off | generated pitches release on trigger release (shared pitches need last owner) |
| Sustain | CC64 ≥64 holds, <64 releases (global flag, not per-channel) |
| Panic | CC120 / CC123 release tracked generated notes |
| Audio | ChordEngine produces no audio of its own |

**Velocity default decision:** the product brief's earlier `Fixed / 100` claim and
the embedded FL reference's `Dynamic / 100` claim are both retained in
`Docs/DEFAULT_BEHAVIOR_DISCREPANCY.md`. For the native GUI milestone, the user's
explicit instruction selected **Dynamic / 100**. Verify that visible default; do not
change it during this test.

## Recording template

```
Date / tester / Mac model / macOS version / Logic version:
Archive SHA-256 verified: yes/no (AU: ea2c072c...)

Step 4  Plug-in Manager: Music-Prod → ChordEngine = valid / failed / n/a
Step 9  Trigger note/velocity → observed output pitches/velocity/channel
Step 10 Raw trigger suppressed? yes/no/unclear (+ evidence)
Step 11 Chord reaches downstream instrument? yes/no/unclear (+ evidence)
Step 12 key: ... scale: ... preset: ... transpose: ...
        velocity mode observed as default: Dynamic / Fixed / other
        release: ... sustain: ... panic: ...
Step 13 ChordEngine silent? yes/no
Step 14 Unexpected behavior / errors / crashes:
Overall: pass / fail / blocked, with unknowns listed
```

## Out of scope for this session

No licensing/trial work, no distribution signing, no notarization, no publishing,
no changes to HISE projects or reference artifacts, and no changes to the native
velocity default without a newer authoritative product specification.
