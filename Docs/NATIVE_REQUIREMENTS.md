# ChordEngine Native — implementation requirements

**Version:** development scaffold `0.1.0` (not a customer release)

This is a code-driven JUCE/CMake product. HISE is a read-only behavioral reference only; neither HISE nor Projucer may be required to configure/build the native project. The complete evidence inventory and confidence labels are in [REFERENCE_FUNCTIONAL_INVENTORY.md](REFERENCE_FUNCTIONAL_INVENTORY.md).

## A. Must match exactly

“Match” means the behavior/identity is approved by the owner and confirmed by reference testing. Source-only hints in the inventory are not yet a behavior contract.

### Stable product and plugin identity

- Customer-facing product: **ChordEngine**.
- Manufacturer displayed in plugin metadata: **Music-Prod**.
- macOS AU: MIDI Processor (`aumi`), bundle ID `com.myCompany.product`, manufacturer code `Abcd`, subtype `Abcd`.
- VST3 on macOS and Windows: bundle ID `com.musicprod.chordenginefl`, manufacturer code `Musc`, plugin code `Cefl`.
- Preserve the identifiers above and any verified licensing product/tenant identifiers; do not derive new identifiers from the name or development version.
- User-facing name has no “FL” suffix.

### Observable behavior after characterization

Once controlled reference tests establish these outputs, preserve them: note transformations and order, timing/sample offsets, velocity rules, channels, note-off ownership, duplicate/shared-note handling, sustain/controllers, bounds, transposition modes/ranges, preset/parameter defaults, state save/restore, and cleanup/panic behavior. The current checked script is insufficient evidence to fill these contracts; do not claim parity or bake untested assumptions into the engine.

### Commercial licensing/trial

- Preserve existing commercial licensing/trial behavior conceptually and preserve its actual server endpoints, product IDs, credentials handling, and user-visible states once verified from the authoritative implementation.
- Do not expose credentials or add a newly invented auth provider/endpoint/trial policy.
- Until the authoritative contract and implementation are supplied/located, show licensing status as unimplemented/unknown in developer documentation; do not ship a substitute.

## B. Can be implemented differently internally

- Use standard modular C++17 classes and a shared core library; JUCE supplies AU/VST3 wrappers and GUI plumbing.
- Use CMake as the only supported product build configuration. Pin JUCE 9.0.3 by exact commit through FetchContent (chosen because JUCE 6.1.3's helper fails against the installed Xcode 26 SDK); do not require Projucer, a JUCE GUI app, or the HISE source tree.
- AU and VST3 can be distinct CMake plugin targets to retain their distinct stable compatibility identities while linking the same processor, editor, core, state, and future licensing boundary.
- Use explicit fixed-size or otherwise real-time-safe data structures in audio/MIDI processing. Avoid allocation, blocking I/O, and network work on the audio thread.
- Host state may be encoded in a native schema rather than HISE data structures once equivalence is demonstrated. Version the schema and preserve relevant user-visible state, not legacy serialization internals.
- Build the plugin as a MIDI effect with MIDI input/output, `IS_SYNTH FALSE`, AU MIDI Processor type, and no deliberate audio generation. The current native processor generates the reference-informed chords and the JUCE editor provides the musical controls; manual host GUI verification remains separate.
- Provide portable core unit tests plus host-level golden-vector tests; test internals need not mirror HISE callback names.

## C. Unknown and requires behavioral testing

Items below block a parity claim or product behavior implementation until resolved:

1. Whether the deployed reference AU actually advertises/behaves as an `aumi` MIDI effect; generated HISE config inspected in the source tree conflicts with the requested AU identity.
2. Whether the existing working implementation transforms notes at all: checked `ScriptProcessor1.js` has no active MIDI callbacks and does not invoke its chord-send functions.
3. Actual available scales, key choices, chord bank contents, UI preset mappings, and default selected preset.
4. Whether transpose targets trigger notes, chord tones, octaves, or multiple destinations; target choices and allowable octave/semitone bounds.
5. Actual note-on/off mapping, output vectors, sample-accurate timing, velocity mapping, range handling, channel mapping, duplicate/shared-note semantics, polyphony, sustain/CC, panic, bypass and reset rules.
6. Recorder availability/behavior; Help & Feedback UI/actions; any further controls absent from the inspected main script.
7. Whether a HISE Sine Wave Generator is active in the target plugin, and whether any audio is emitted directly.
8. Which user settings are persisted in presets and host state; parameter IDs/automation contract.
9. Commercial licensing/trial implementation, server contract, grace/offline/expiry rules, data migration, and exact HISE data-folder macro used in the shipped binary. The framework path derivation is in the inventory but does not prove ChordEngine product licensing.
10. Whether AU and VST3 (including the FL-branded compatibility wrapper) exhibit the same MIDI behavior.

Required reference collection: capture a disposable host session using an event logger/virtual MIDI destination; record note/controller status, channel, velocity, sample/time position and audio RMS while enumerating every UI option. Save/reopen a project and instance to measure state. For licensing use controlled test accounts only and preserve secrets locally.

## D. Legacy HISE details that must NOT be copied

- No dependency on HISE, HISE scripting, HISE project export, generated HISE `Plugin.cpp`, generated `JucePluginDefines.h`, Projucer, HISE `ScriptProcessor` ownership conventions, `Synth.addNoteOn` APIs, or HISE data serialization.
- Do not restructure the product around the Sine Wave Generator or nested ScriptProcessor2. The two processor scripts serve different graph locations; the second is empty in the inspected source.
- Do not copy HISE’s generated company placeholders (`My Company`, `yourcompany.com`, `(c)2017, Company`) into native customer-facing metadata or licensing identity.
- Do not treat the single test chord table or inert helper functions as a complete, production behavior contract.
- Do not port generated UI XML as the native UI design or build an imitation of the full UI before core behavior is specified and tested.
- Do not copy SideChainer’s DSP, authentication source, or product logic. It is a build-convention reference only.

## Initial milestone acceptance criteria

- CMake configures with pinned JUCE 9.0.3 and no HISE/Projucer dependency. **Met on this macOS host.**
- macOS builds AU and VST3 locally (arm64 + x86_64), with no Developer ID/certificate signing, install, or copy-to-host-system step; Windows CMake condition builds VST3 only. **Build and static architecture/metadata checks passed; the Apple linker may add an ad-hoc Mach-O signature. No DAW discovery/routing test has been performed.** Windows configuration remains untested on Windows.
- AU static metadata is `aumi`, `Abcd/Abcd`, bundle `com.myCompany.product`; VST3 metadata is `Musc/Cefl`, bundle `com.musicprod.chordenginefl`.
- Both identify as ChordEngine / Music-Prod, development version 0.1.0; accept and emit MIDI, are not synths; do not generate audio intentionally. **Static configuration/build verified; MIDI-through behavior and silent audio have not been exercised in a DAW host.**
- The initial MIDI path passes input MIDI through unchanged; the GUI states this is a development skeleton. Processor-level CTest verifies unchanged event bytes/order/sample offsets and zeroed audio buffer.
- Core/processor/reference-helper tests run under CTest (**2/2 passed**). No runtime parity claim for key/scale/chord event processing/recording/licensing is made.
