# ChordEngine Native core reference

**Reference evidence:** `Tests/FLReferenceVectors.json` and [FL_REFERENCE_BEHAVIOR_CAPTURE.md](FL_REFERENCE_BEHAVIOR_CAPTURE.md) describe 38 deterministic calculations from the script embedded in the approved ChordEngine FL 0.4.0 VST3. They remain **source-derived, not host-observed** (`hostObserved: false`). Separately, the native AU's basic MIDI routing and chord generation were manually confirmed in Logic on a clean second Mac; see [GUI_MILESTONE_HOST_OBSERVATION.md](GUI_MILESTONE_HOST_OBSERVATION.md). Passing native tests establishes correspondence to source calculations, not exhaustive runtime parity in a DAW or plugin format.

## Proven from reference source and implemented

### Scales and keys

`ScaleSystem` contains only the eight embedded tables: Major `[0,2,4,5,7,9,11]`, Minor `[0,2,3,5,7,8,10]`, Dorian `[0,2,3,5,7,9,10]`, Phrygian `[0,1,3,5,7,8,10]`, Lydian `[0,2,4,6,7,9,11]`, Mixolydian `[0,2,4,5,7,9,10]`, Locrian `[0,1,3,5,6,8,10]`, and Harmonic Minor `[0,2,3,5,7,8,11]`. Key indexes 0–11 are C through B and add their semitone offset to the scale degree. These are literal fixture values, not inferred patterns.

### Presets and chord resolution

Exactly 12 named presets are registered in the embedded source order: Basic, Pop, Piano, Emotional, Dreamy, Cinematic, R&B, Neo Soul, Lo-Fi, House, Deep, Ambient. Each has its own 73-entry map for valid trigger notes 24–96. Mapped values select a one-based scale degree and one of the ten embedded chord templates; the resolver stacks scale degrees, honors literal interval overrides, applies the source extension-inclusion rules, and returns at most six intervals. Preset voicing is close except Cinematic=spread and Ambient=open. Trigger octave selects the map entry; the output root is MIDI 48 plus key index plus the mapped scale-degree offset.

The maps and template pairs are typed immutable data in `Source/Core/ChordPresetMaps.inc`, generated from the fixture's `sourceTables`; there is no script engine, HISE dependency, GUI dependency, or host API in the core. All 38 cases are compared against the native event core by `ChordEngineFLReferenceVectorTests`.

### Trigger, velocity, and transpose

- The accepted trigger range is inclusive MIDI 24–96. Note-on and note-off inputs in/outside the MIDI trigger range are suppressed; out-of-range note-ons generate no chord.
- Dynamic uses input velocity (clamped to 1–127); Maximum emits 127; Fixed uses its 1–127 configured value. Native development defaults are key C, Minor, Dreamy, WHOLE 0, Dynamic, fixed value 100.
- **Prompt/spec discrepancy:** this task brief said the source default is Fixed/100, but the verified embedded release capture and fixture `defaultStateSource` say Dynamic/100. The core follows the artifact-backed Dynamic default and does not silently relabel the source.
- WHOLE, LOWEST, and HIGHEST accept octave steps -2 through +2. The complete chord is first shifted by octaves as needed to fit MIDI 0–127. WHOLE then shifts all pitches by `12 * steps` and repeats whole-chord normalization. LOWEST/HIGHEST shift the first minimum/maximum pitch respectively and clamp only that pitch; output order may therefore become unsorted. Duplicate pitches are compacted while retaining first-occurrence order.

### MIDI voice state and controllers

`ChordEngineCore` stores up to six generated pitches per input `(channel, trigger-note)` and reference-counts each generated pitch per output channel. Matching note-off uses the stored pitches and emits an output note-off only when the last owner of that channel/pitch is released. Different trigger voices can coexist. CC64 uses the source's **single global sustain flag**: values >=64 hold released voices, values <64 release sustained voices. CC120 and CC123 release all owned pitches and clear voices and sustain state. Output MIDI preserves the input channel.

The core is fixed-capacity/allocation-free in its processing path. Native processor output is emitted at the incoming JUCE sample position to preserve the existing block timeline and input event order. This is a native timing policy, not a host-observed claim about the embedded HISE bridge's `Synth.addNoteOn/Off(..., 0)` offset argument. Unhandled MIDI is forwarded unchanged by the processor, consistent with the existing source-static bridge evidence; effective host filtering is unknown.

## Unresolved / not implemented as finished behavior

- Retriggering the same channel/trigger while global sustain is held has suspicious ownership behavior in the embedded source: release marks the old voice sustained, then registration overwrites the slot. The core reports this specific event as `unresolved` and does not invent replacement state or generated notes.
- MIDI output actually observed in any DAW, host event ordering/filtering, exact host timestamps, AU behavior, input/output routing, audio behavior under a host, and save/restore persistence are unverified.
- The native GUI milestone is now implemented with real processor-backed musical controls and a generated-MIDI recorder. Licensing/trial backend, host state migration/persistence, and host-specific workarounds remain deferred; GUI runtime still needs manual Logic verification.
- The brief's Fixed/100 default conflicts with the artifact-backed reference. The native GUI milestone retains Dynamic/100 per explicit user direction; see [DEFAULT_BEHAVIOR_DISCREPANCY.md](DEFAULT_BEHAVIOR_DISCREPANCY.md).

`hostObserved` for the source-derived fixture remains `false`. A separate manual native AU Logic observation exists, but do not describe these fixture tests as host parity, exhaustive AU/VST3 compatibility, or DAW validation.
