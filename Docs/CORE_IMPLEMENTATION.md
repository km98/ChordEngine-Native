# ChordEngineCore implementation

## Architecture

`ChordEngineCore` is standard C++17 and depends only on project core/MIDI headers. It has no JUCE, HISE, GUI, AU/VST3, platform API, or DAW dependencies. The JUCE processor translates host MIDI to/from the plain `chordengine::midi::NoteEvent`; the editor reads and updates processor state through validated snapshots/accessors, without accessing MIDI buffers. GUI work never runs in `processBlock()`.

## Behavior and test evidence

- `CoreTests.cpp` covers all eight scale intervals, key bounds, all twelve preset IDs/maps, source defaults, key shift, valid trigger output/channel/sample offset, trigger boundaries, three velocity modes, transpose target/step combinations, matching releases, simultaneous voices, global sustain, CC120/123 panic, deterministic repeat, processor conversion, and silent audio.
- `CoreReferenceTests.cpp` checks all scale names/degrees, all preset identities/voicings, and all 876 map entries against the source-derived tables.
- `FLReferenceVectorTests.cpp` parses the 38 source-derived fixture cases, configures the native core per case, feeds note-on and matching isolated note-off through `ChordEngineCore`, and compares pitch order, channel, velocity, event count and native sample offset. `Tests/FLReferenceVectors.json` remains `hostObserved: false`; these calculations are not host-captured parity evidence.
- `ProcessorIntegrationTests.cpp` exercises the real JUCE processor block, all GUI-exposed state accessors and their effect on next-block processing, source-default cases, recorder arm/cancel/capture, exported MIDI timing/stop closure, index validation, and newest-first rolling three-take storage.
- CTest includes four tests: core behavior, reference tables, all 38 source-derived vectors, and processor integration. The most recent GUI milestone run passed 4/4 CTest tests, 17/17 processor integration cases, and 38/38 source-derived vectors.

## Native GUI boundary

The native JUCE editor exposes Key, Scale, Chord Preset, piano preview/live-note display, transpose target/octave, velocity mode/value, recorder UI, INFO placeholders, and Help & Feedback. The processor's packed atomic configuration snapshot is read by the editor and applied to `ChordEngineCore` at the next process-block boundary. Recorder output is generated chord MIDI only; capture uses fixed storage and the audio callback does not wait for GUI/file work. MIDI file creation and external drag are message-thread operations.

The native AU MIDI path was manually verified by Martin in Logic Pro on a clean second Mac: host recognition as Music-Prod → ChordEngine MIDI FX, input, expected chord for a valid trigger, and routing to downstream Classic Electric Piano. This is human-observed for the tested case only, not automated DAW testing. See [HostObservations.json](../Tests/HostObservations.json). GUI appearance, controls, resize, recorder file-drop behavior, and VST3 host behavior still need host testing.

## Deferred / out of scope

Host state serialization/restore, licensing/account/trial backend, internal audio/synthesis, complete product-release parity, installer creation, signing, notarization, installation, and publishing remain out of scope. Existing default discrepancy documentation is retained: GUI native default Dynamic / 100 follows artifact-backed reference evidence; product confirmation of the contradictory Fixed / 100 brief remains deferred.
