# ChordEngine FL 0.4.0 reference behavior capture

**Reference evidence status:** `VERIFIED-SOURCE-STATIC` findings only; no DAW/host MIDI capture of the approved FL reference was performed. This report is based on the script embedded in the approved release VST3 payload, not on the stale editable project script. Separately, the native AU's critical Logic routing was manually observed as recorded in [GUI_MILESTONE_HOST_OBSERVATION.md](GUI_MILESTONE_HOST_OBSERVATION.md). That native observation does not relabel the FL reference fixtures or verify saved-state reload, exhaustive vectors, or format parity.

## Safety and evidence provenance

No reference project or artifact was changed, rebuilt, installed, or opened in a DAW. No GUI automation was performed: the required persistent `TESTING; DO NOT TOUCH` warning gate was not established, so host verification was stopped rather than bypassed. The native runtime remains inert; no transformation was wired.

The approved release is the universal VST3 executable at [`ChordEngine FL`](../../HISE%20Projects/ChordEngine-FL/Build/Release/ChordEngine-FL-0.4.0/Mac/ChordEngine%20FL.vst3/Contents/MacOS/ChordEngine%20FL), SHA-256 `3ffbe1f349846df64dc7e2cf3357b1e682e4f8f1dd3cf8a879080edc773483ed` (arm64 + x86_64). The main embedded preset payload was extracted read-only from generated [`PresetData.cpp`](../../HISE%20Projects/ChordEngine-FL/Binaries/Source/PresetData.cpp) `temp19[51381]` and successfully decompressed with the matching HISE preset dictionary. The compressed payload SHA-256 is `da7927b596211213efb8b1dcfa88c811ef3aa328eb83597b4e44113297ba63fa`; that exact 51,381-byte payload was found inside the approved Mach-O at byte offset 16,929,040. Its main ScriptProcessor1 script is 205,472 bytes, SHA-256 `5ba711582e87c75b4c38322152055b12bc6ef6c92dc9ba6444d1cbda34b660da`.

**Important source divergence:** the editable FL [`ScriptProcessor1.js`](../../HISE%20Projects/ChordEngine-FL/Scripts/ScriptProcessors/ChordEngine/ScriptProcessor1.js) is 953 lines, has SHA-256 `bd84bf44c64828ad5dbd649f713f46665d018068078d284f9b9ec430844a4ce6`, and is the old UI/helper-only code shared with the original HISE project. It has no MIDI callbacks. The release-embedded script does contain full MIDI callbacks, scales, presets, transpose and velocity behavior. Do not use the old script’s helper formulas as if they were the release runtime. All embedded-script line numbers below refer to the script recovered from the approved release payload; it was written only under `/tmp` for inspection. The original project source and the FL project remain untouched.

The bridge evidence is separately from protected FL [`Plugin.cpp`](../../HISE%20Projects/ChordEngine-FL/Binaries/Source/Plugin.cpp) (MD5 `1044c2719acb8d49573c5aadd9e7ca22`). Its code forwards non-ignored NoteOn/NoteOff/Controller/PitchBend/Aftertouch/ProgramChange/AllNotesOff events from the main synth-chain buffer to host MIDI output, skips ignored and timer events, and clamps event timestamps to the current block. This is implementation evidence, not a host output capture. The source comment expects the raw trigger to be ignored; the embedded release script confirms that for note-on and note-off.

## Classification

- **VERIFIED-SOURCE-STATIC:** explicit code/data in the script embedded in the hash-identified approved VST3; or, for plug-in packaging, its exact generated metadata. This does not mean verified in a DAW.
- **UNKNOWN:** code inspection cannot determine what a host will actually receive, state restoration, or behavior for which the release has no explicit branch.
- **UNSUPPORTED/UNVERIFIED:** the format/product path is not present in this approved artifact or cannot safely be tested here.
- **VERIFIED (host-observed):** none in this milestone.

## 1. Note-on

### VERIFIED-SOURCE-STATIC

- Embedded-script lines 5192–5201: `onNoteOn()` reads input channel, note number and velocity, and calls `Message.ignoreEvent(true)` on the raw trigger. A valid trigger therefore requests chord notes instead of forwarding the trigger as a note. Invalid-range or expired-trial branches also return after suppression, with no generated chord.
- The accepted trigger range is inclusive MIDI 24–96 (C1–C7); 23 and 97 produce zero notes ([1258–1259, 3007–3035, 5211–5229]). Trigger octave selects a preset-specific map entry/degree/template. The output register does not follow trigger octave: the base is C3/MIDI 48 plus selected key and scale-degree offset ([5229–5246]).
- For valid, non-expired inputs, the resolved interval/template data is voiced and sorted, then whole-chord octave-normalized to MIDI 0–127. LOWEST/HIGHEST subsequently shift and clamp one selected pitch and duplicates are compacted; those single-note target modes can therefore leave the emitted sequence no longer ascending. Generated note-ons use the input channel, velocity clamped to 1–127, and HISE sample offset argument 0 ([5327–5425]).
- Velocity mode default is Dynamic. Dynamic uses input velocity; Maximum sets 127; Fixed uses the fixed setting (default 100, UI range 1–127, step 1). Output is clamped to 1–127 ([826–875, 1081–1083, 5403–5425, 5640–5669]). The exact semantics of HISE host-event time and the compiled bridge’s resulting sample timestamp have not been observed.
- Source-selected defaults are key C, scale Minor, chord preset Dreamy, transpose target WHOLE, octave offsets zero, velocity Dynamic, fixed velocity 100 ([1087–1089, 1156–1185, 5016–5110, 5139–5177]).
- The source maps each trigger to a diatonic degree and chord template through a fixed per-preset map. Resolution stacks scale degrees, conditionally includes idiomatic extension intervals, and emits at most six tones ([1212–1285, 2799–3035]). The exact tables and reproducible source-derived cases are in [FLReferenceVectors.json](../Tests/FLReferenceVectors.json).

### UNKNOWN

- Exact MIDI output as actually observed by any DAW/logger, including event order/timestamp after the HISE bridge and host filtering.
- Behavior under trial-expired/license states in the working installation; the embedded source contains an expired-trial early return, but no licensing/host state was changed or tested.

## 2. Note-off, sustain, retrigger

### VERIFIED-SOURCE-STATIC

- `onNoteOff()` also ignores the raw trigger note, calls the voice release for the same input channel and trigger pitch, then flushes queued generated note-offs ([5464–5499]). Stored generated pitches are used for releases rather than recalculating the chord from current selector state ([3040–3130]). Thus source intent pairs release with the notes registered at note-on; `Synth.addNoteOff` uses sample offset argument 0 ([2694–2705]).
- With sustain off, triggering the exact same channel+pitch again releases its previous stored voice before registering its replacement ([3040–3068]). Merely pressing a different chord does not globally release an earlier chord; voices are keyed by channel and trigger pitch. With sustain on, `ceReleaseVoice` marks that voice sustained and returns; the subsequent same-slot registration overwrites its voice slot without flushing those releases, so retrigger-under-sustain ownership/release behavior is potentially problematic and remains UNKNOWN pending host capture.
- Shared generated pitches are reference-counted per channel and only produce a note-off when the final owner releases ([3040–3130]).
- Sustain is a single global flag, not per MIDI channel: note-off while held marks the voice sustained; CC64 >=64 sets sustain, and CC64 <64 releases retained voices ([5502–5522; helper definitions 3040–3140]). CC120 and CC123 invoke a panic that queues releases for tracked notes and clears ownership/sustain ([5527–5552]).

### UNKNOWN

- Actual host-observed note-off matching, shared-note timing, repeated-note semantics or DAW behavior (notably FL Studio block handling). No MIDI monitor capture was run.
- Sustain behavior under interleaved channels or pedal transitions as received by the host.

## 3. Controllers and other MIDI

### VERIFIED-SOURCE-STATIC

- The script explicitly acts on CC64, CC120 and CC123 as described above. It does not call `Message.ignoreEvent` from `onController`; there is no explicit source transformation/filter for other CC numbers ([5502–5554]).
- There are no release-script callbacks for pitch bend, channel/poly aftertouch or program change. The C++ bridge recognizes and copies pitch bend, aftertouch, program change, CC and all-notes-off events if they remain non-ignored in the event buffer ([protected `Plugin.cpp`](../../HISE%20Projects/ChordEngine-FL/Binaries/Source/Plugin.cpp#L97-L118)). Its `processBlock` appends those surviving events to the incoming MIDI buffer after base frontend processing, and clamps timestamps to `numSamples - 1` if needed ([lines 80–118]). The exact post-base surviving event stream and timestamp effect are not host-observed.

### UNKNOWN

- Whether unhandled CC, pitch bend, aftertouch, program change and other events reach the host unchanged in practice. Static bridge switch cases are not an observation of MIDI output.
- Exact host-observed note-on/off event ordering and timestamps, including the bridge’s clamping result and host filtering.

## 4. Scales and generated pitches

### VERIFIED-SOURCE-STATIC

The release’s key selector provides 12 choices (C, C#, D, D#, E, F, F#, G, G#, A, A#, B); the key index is added to the selected scale-degree root. The scale selector provides exactly these 8 labels with 7 stored semitone offsets from the selected key:

| Scale | Embedded offsets |
|---|---|
| Major | 0, 2, 4, 5, 7, 9, 11 |
| Minor | 0, 2, 3, 5, 7, 8, 10 |
| Dorian | 0, 2, 3, 5, 7, 9, 10 |
| Phrygian | 0, 1, 3, 5, 7, 8, 10 |
| Lydian | 0, 2, 4, 6, 7, 9, 11 |
| Mixolydian | 0, 2, 4, 5, 7, 9, 10 |
| Locrian | 0, 1, 3, 5, 6, 8, 10 |
| Harmonic Minor | 0, 2, 3, 5, 7, 8, 11 |

These are literal embedded table entries, not inferred theory ([1191–1210]). Chord tones are generated by stacking diatonic scale degrees from the mapped scale degree, with explicit template overrides and an extension filter ([2799–3035]). All 8 scales have a C4 trigger-derived fixture in [FLReferenceVectors.json](../Tests/FLReferenceVectors.json). These are deterministic source calculations, not host-observed captures.

## 5. Transpose, range and bounds

### VERIFIED-SOURCE-STATIC

- The selector has three targets: WHOLE, LOWEST and HIGHEST. Each target stores an independent octave-step value per chord preset. The stepper clamps to -2…+2 inclusive, i.e. -24…+24 semitones; every preset/target initializes to zero ([1140–1185, 1389–1405, 2601–2618, 5094–5110]).
- WHOLE shifts every generated pitch by 12 semitones per step before whole-chord range normalization ([5271–5286]). LOWEST shifts the first minimum pitch by that amount; HIGHEST shifts the first maximum pitch. These single-tone modes then clamp that selected pitch to MIDI 0–127; other tones are left untouched ([5327–5395]).
- Before LOWEST/HIGHEST adjustment, the complete voicing is shifted by octaves until all pitches fit 0–127 ([5288–5325]); it is not per-note clamping. Cases cover both ends of the transpose range, the HIGHEST +2 clamp, and trigger bounds in the vector fixture.

- These are explicitly transpose *octave* steps; the `ceTranspose` semitone variable remains initialized to zero in this release script and has no user-edit path in the inspected embedded script. No WHOLE ±1-semitone control is evidenced.

### UNKNOWN

- Actual rendered MIDI at each range boundary and effective DAW response. Static arithmetic vectors do not establish host behavior.

## 6. State and presets

### VERIFIED-SOURCE-STATIC

- The embedded source contains 12 chord presets: Basic, Pop, Piano, Emotional, Dreamy, Cinematic, R&B, Neo Soul, Lo-Fi, House, Deep, Ambient. Each has a fixed trigger map, and preset-specific voicing is close except Cinematic=spread and Ambient=open ([1247–1255, 1261–1285]).
- Factory initialization selects C / Minor / Dreamy; all octave targets for all presets reset to zero. Velocity mode defaults Dynamic and the displayed fixed-velocity value is 100 ([1087–1089, 826–875, 5016–5110, 5139–5177]).
- Key, scale and preset combo components are explicitly configured with `saveInPreset: false` ([root/scale/preset controls around lines 200–310]); octave stores are script-side arrays reset during `ceInitialize`; velocity settings are script registers/control values. Thus the embedded script itself does not save these values through those combos or maintain these octave arrays across script initialization. This establishes the script-side persistence wiring only, not whether JUCE/HISE host state serializes equivalent parameter state independently.

### UNKNOWN

- What HISE/JUCE host state saves independently, whether any user preset/state mechanism serializes these controls, and whether state survives save/close/reload. This requires a safe disposable host test; none occurred.
- Whether the factory selection/default state shown by static initialization is the state presented by the actual installed instance after host restore.

## 7. Host and format behavior

### VERIFIED-SOURCE-STATIC

- The inspected approved artifact is a VST3 only: bundle identifier `com.musicprod.chordenginefl`, version 0.4.0, universal arm64+x86_64. Generated FL metadata says synth=1, wants MIDI input=1, produces MIDI output=1, MIDI-effect=0, VST3 category Instrument ([`JucePluginDefines.h`](../../HISE%20Projects/ChordEngine-FL/Binaries/JuceLibraryCode/JucePluginDefines.h), release [`Info.plist`](../../HISE%20Projects/ChordEngine-FL/Build/Release/ChordEngine-FL-0.4.0/Mac/ChordEngine%20FL.vst3/Contents/Info.plist)). The project XML also requests MIDI input/output and VST3 support, while its saved legacy bundle/plugin IDs disagree with generated release metadata ([`project_info.xml`](../../HISE%20Projects/ChordEngine-FL/project_info.xml)).
- The bridge copies eligible main-chain events into its output buffer after frontend processing ([protected `Plugin.cpp`](../../HISE%20Projects/ChordEngine-FL/Binaries/Source/Plugin.cpp#L80-L118)). This means source has an output bridge for generated events and selected input events; it does not prove what reaches an external host.

### UNSUPPORTED/UNVERIFIED

- The approved ChordEngine FL 0.4.0 release folder inspected here contains only its VST3, no AU component. AU behavior or VST3-vs-AU differences cannot be asserted from this artifact; no AU host test was performed.
- Exact host routing/input delivery, output enumeration and event acceptance in FL Studio or any other DAW remain UNKNOWN. Generated project configuration and wrapper flags are not runtime routing measurements.

## Golden vectors and tests

[FLReferenceVectors.json](../Tests/FLReferenceVectors.json) contains 38 deterministic source-derived cases covering all eight scales, all 12 presets, trigger bounds, WHOLE/LOWEST/HIGHEST octave extremes, a high-note clamp, and Dynamic/Maximum/Fixed velocity. Each case distinguishes generated pitches/velocity from the suppressed raw trigger and records source note-off pitches and channel under an explicit isolated-trigger fixture assumption: one active trigger, no overlapping voice sharing an output note, sustain off, and no host filtering. `hostObserved: false` is intentional: these are **not** promoted as true runtime golden captures until checked against MIDI output from a protected, safely isolated host session.

Existing [GoldenVectors.json](../Tests/GoldenVectors.json) remains the original-HISE helper inventory and must not be substituted for the FL release behavior. Existing C++ tests exercise the old helper model and intentionally keep runtime `process()` unsupported; no native runtime logic or test claims were changed here.

## Exact next milestone

**Next reference-behavior milestone:** if broader parity is required, safely verify more of the protected FL VST3 behavior with timestamped input/output captures for the 38 source-derived cases plus CC/retrigger/sustain/state-reload checks. The native AU's basic Logic routing is already manually verified, but this does not establish these broader reference outputs. Keep the fixture vectors marked `hostObserved=false` until those exact FL-reference cases are captured.

## Protected artifact checks

Rechecked after inspection: approved FL VST3 SHA-256 remains `3ffbe1f349846df64dc7e2cf3357b1e682e4f8f1dd3cf8a879080edc773483ed`; protected FL [`Plugin.cpp`](../../HISE%20Projects/ChordEngine-FL/Binaries/Source/Plugin.cpp) MD5 remains `1044c2719acb8d49573c5aadd9e7ca22`. The original AU project was not opened or changed. Only `/tmp` was used for temporary extraction; no generated reference copy was written into either project.
