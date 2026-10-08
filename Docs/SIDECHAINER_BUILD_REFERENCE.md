# SideChainer build reference (read-only findings)

**Inspected project:** `/Users/martin/Documents/SideChain/SideChain`. This project was not modified and is a Projucer project, not a CMake project.

## Findings

- `SideChain.jucer` uses JUCE **6.1.3** and C++17; the Windows handoff confirms the modules version and identifies the JUCE-bundled VST3 SDK.
- Formats in the project: macOS AU and VST3; MIDI input/output/effect are disabled for SideChainer. Its stable plugin IDs are distinct from ChordEngine and must not be copied: manufacturer `Musc`, code `SdCh`, bundle `com.musicprod.sidechain`.
- Layout is conventional JUCE source: `Source/`, `Resources/`, `Tests/`, `JuceLibraryCode/`, and `Builds/MacOSX/` with generated Xcode project/build directories. CMakeLists/CMakePresets were not found in the project.
- The `.jucer` uses a machine-specific `../../HISE/JUCE/modules` path. The handoff warns Windows developers to supply exactly JUCE 6.1.3, use the bundled VST3 SDK, and not treat its proposed Windows build as already verified.
- The Mac exporter selects universal `x86_64,arm64`, and `enablePluginBinaryCopyStep="0"` for both configurations. `hardenedRuntime="0"`; this is a local development/build convention, not a recommended release security or signing policy.
- No signing/notarization policy is encoded as a reusable convention in the inspected SideChainer settings. Do not infer a release policy from disabled hardened runtime or binary copy.
- JUCE 6.1.3’s CMake helper is present in upstream source and maps `IS_MIDI_EFFECT TRUE` to AU main type `kAudioUnitType_MIDIProcessor` (`aumi`), and permits CMake use without the Projucer GUI.

## Recommendation for ChordEngine

The first configure/build using the known Music-Prod JUCE 6.1.3 choice failed before reaching project code: Xcode 26.3’s macOS 26.2 SDK marks `CGWindowListCreateImage` unavailable, and JUCE 6.1.3’s `juceaide` source still calls it. Setting a macOS deployment target does not avoid the availability error because Xcode compiles against the installed SDK headers.

For this initial native project, pin **JUCE 9.0.3** instead. Its release notes include fixes for current Apple SDK compatibility, and it supports CMake, AU MIDI Processor/VST3, macOS universal and Windows VST3. CMake 3.22 is the minimum for this pin; a project-local CMake 3.31.10 is available. This is a documented, evidence-driven exception to SideChainer’s known JUCE version: reproducing its 6.1.3 version is less valuable than a successful supported build with the current installed Xcode. The plugin API/host behavior still needs validation, and the change does not claim SideChainer was validated on Windows.

If a clean JUCE 9.0.3 configure/build or the target host checks fail, pause and evaluate alternatives instead of patching JUCE. Independently confirm JUCE’s current commercial licensing terms before distribution; this milestone does not settle framework licensing.

## Dependency strategy in the new project

CMake FetchContent pins the JUCE 9.0.3 release commit `be29c81492b6151c8ea8d14c840e1311963b3a83`; it does not require the global JUCE GUI app. The local HISE-source JUCE checkout is dirty and lacks its top-level CMake file; it is not a valid dependency tree. The clean upstream source is fetched into the new project’s `Build/` tree.

Before any commercial distribution, independently confirm current JUCE licensing terms applicable to the commercial product; this scaffold does not settle framework licensing.
