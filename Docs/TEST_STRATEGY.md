# ChordEngine Native — behavior test strategy

## Status and rule

The native suite now covers source-proven chord-data/formula helpers and retains the earlier plugin/MIDI-tracker checks. No runtime parity claim is made. The checked HISE `ScriptProcessor1.js` defines test-bank/chord helper functions but has no MIDI event callback and does not invoke `ceCalculateNotes`, `ceSendNoteOn`, or `ceRegisterVoice`, so expected product event vectors cannot be responsibly inferred from that source alone. First obtain observations from the actual working reference plugin in a disposable DAW session.

## Reference capture format

For each test case, record:

- reference artifact/version/hash and format (AU, original VST3, FL-compatible VST3);
- DAW/version, OS/architecture, sample rate, buffer size, tempo/transport state;
- full ordered input event stream: event kind, channel 1–16, pitch/controller, value/velocity, sample offset or timestamp;
- full ordered output event stream in the same fields;
- direct audio output peak/RMS with downstream instrument disconnected;
- all relevant selected key/scale/preset/transpose/velocity/record settings and before/after persisted host state.

Keep secrets out of fixtures and logs. Store stable golden vectors as text/JSON fixtures in `Tests/Fixtures/` after owner review; do not create expected vectors from a theory or from the HISE helper table alone.

## Deterministic behavior matrix

| Area | Test vectors / cases | Status |
|---|---|---|
| Key and scale selection | Each advertised key × each scale, root-note and chromatic inputs, accidentals, edge pitches; compare exact generated pitch sets | Blocked on reference UI/runtime and golden outputs |
| Each chord preset | Select each named preset; trigger each documented chord slot; record pitch set/order, velocity and duration | Blocked; current source has one test bank and no wired preset callback |
| Chord-note generation | Baseline, octave boundaries, close/open/spread voicing if exposed, clamping 0/127, sorted/unison output | Helper suggests possible cases; exact product vectors blocked |
| Transpose target | Each target/mode independently (trigger, chord tones, or other advertised target), zero and +/- semitone, affected note-off pairing | Blocked on actual control contract |
| Transpose octave | Every octave step and min/max, clamped/out-of-range outputs, interaction with scale/preset | Blocked on range/target contract |
| Note-on | velocity 1/64/127, each channel, low/high note, zero-velocity note-on, event offsets inside a block | Behavior unknown; capture required |
| Note-off | matching note-off, unmatched note-off, repeated note, different velocity, overlap across blocks | Behavior unknown; capture required |
| Multiple simultaneous inputs | Distinct pitches, same pitch on separate channels, chord overlaps, order permutations | Behavior unknown; capture required |
| Duplicate notes/ownership | repeated same trigger, shared output pitch across triggers, release in both orders, note-off after replacement | Behavior unknown; helper design is not proof |
| Sustain/controllers | CC64 values 0/63/64/127, pedal-up with held/retriggered notes, channel isolation, CC120/123 | Behavior unknown; capture required |
| Channel behavior | channels 1 and 16 plus all intermediate channels; no channel remap; channel-specific controller | Behavior unknown; capture required |
| Timing | offsets 0, block-end, next-block, rapid notes, host tempo/transport if relevant; compare timing tolerance | Behavior unknown; capture required |
| Velocity modes | Dynamic, Maximum, Fixed and fixed values 1/64/100/127 across input velocities | UI labels/source defaults exist, behavior not wired in checked script |
| Preset/state persistence | default instance, switch, save/reload preset, host project save/reopen, automation, schema migration | No native legacy contract yet |
| Recorder | capture/play/stop/clear, quantization/loop/tempo if exposed, repeated playback, persistence | Not present in checked script; find actual runtime behavior |
| Reset/cleanup | panic, bypass, transport stop, releaseResources, plugin removal; assert no hanging output notes | Behavior unknown; native tracker reset tested only |
| Audio neutrality | no downstream instrument; render silence through MIDI effect for all paths/operations | Product reference unknown; native skeleton clears audio |
| Licensing/trial | activated, fresh, expired, offline/grace, malformed response, logout/change machine as contract permits | No safe test contract/credentials in inspected project; owner supplies implementation and controlled test accounts |
| Help & Feedback | presence, activation, navigation target and offline error behavior | Not present in checked script; inspect actual running build |

## Native automated tests in milestone 0.1.0

`Tests/CoreReferenceTests.cpp` reads `GoldenVectors.json`, verifies all twelve chord entries/defaults and each helper-derived output vector, checks trigger pitch-class mapping for every MIDI pitch 0–127 plus C2–C6 examples, validates close/open/spread voicing and uniform transpose/clamp vectors, and confirms the named `minor` label still has no guessed interval pattern. It explicitly asserts valid input processing produces no native events until runtime behavior is measured.

`Tests/CoreTests.cpp` verifies core reset, helper-derived default output, uniform whole-note arithmetic, unsupported `lowest`/`highest` targets and unknown octave range, event no-op behavior for note-on/note-off/multiple notes, tracker lifecycle (note-on, channel-separated same pitch, repeated same pitch accounting, sustain hold/release, zero-velocity note-on, channel/note upper boundary, invalid query channels, reset), plus the JUCE processor’s event-byte/order/sample-offset pass-through, tracker observation, and audio clearing.

These tests exercise the processor class but do not load the built AU/VST3 inside a DAW host; host plugin discovery and real host routing remain unverified. They do not assert chord/scale/preset parity. Add as requirements mature:

1. Pure C++ core test vectors from reviewed runtime reference captures (current golden data covers only helper formulas, not DAW event output).
2. MIDI-block transformation tests preserving stable order and offsets where expected.
3. Golden round-trip state tests and parameter-ID stability tests.
4. Host validator and real DAW tests for AU MIDI Processor and each VST3 host.
5. A MIDI event logger plus silent-audio meter in the disposable host integration session.
6. Licensing unit/integration tests only around the supplied existing contract, with secrets injected at test runtime and never stored in source.

## Acceptance / parity gates

- Every advertised scale/chord preset and target/range has at least one deterministic reviewed reference vector; boundaries and interactions are covered.
- Exact note/channel/velocity ordering matches, or an explicitly approved and documented tolerance is agreed for event time. No dropped/stuck notes or unexpected audio.
- State/preset and licensing behavior have explicit migration/compatibility approvals.
- AU + VST3 metadata are verified separately and hosts confirm intended routing.
- Only after these gates can the team describe the native implementation as behaviorally equivalent.
