# ChordEngine Native (development build)

Code-driven JUCE/CMake rewrite workspace. Development version **0.1.0**. The native MIDI engine and customer-facing JUCE GUI are implemented for development testing; this is not a release build and does not claim exact visual parity with HISE or the embedded FL GUI.

- Product identity: **ChordEngine**, **Music-Prod**.
- macOS formats: AU MIDI Processor (`aumi`) + VST3.
- Windows format: VST3.
- Plugin code is shared between targets; AU/VST3 compatibility IDs remain distinct as documented in CMake.
- Native JUCE editor reproduces the reference product layout and terminology: two-tone ChordEngine wordmark with CHORD/INFO navigation, chord well with live readout and C4 preview, Key/Scale/Chord Preset row above a **clickable** C2–C7 piano, TRANSPOSE cluster (WHOLE/LOWEST/HIGHEST, −2…+2 octave, drawn −/+ steppers), VELOCITY MODE and FIXED VELOCITY cards, MIDI recorder with three draggable takes, INTERNAL AUDIO card, Music-Prod wordmark slot, `Chord Engine v0.4.0` footer, and an INFO page (account/trial/license/updates structure, Help & Feedback). The layout is painted in the reference 800×650 design space and mapped through one uniform scale, so it stays faithful from 640×520 to 1280×1040.
- Every visible interactive control is real. The piano keys push real notes into the processor's Core trigger path, the fixed-velocity slider is a real interactive `juce::Slider`, and the recorder/navigation/INFO controls are bound to real actions. No painted fake sliders, toggles, buttons or keyboard keys exist.
- Internal audio is **not** implemented: the plugin is a MIDI effect with no audio bus, so the reference internal-synth switch cannot work without changing the AU/VST3 architecture. The card keeps the reference geometry and caption but publishes the capability as unavailable (`N/A` chip with the reason on screen and in a tooltip); the limitation is documented rather than faked. Account/update/licensing and the trial clock are real and connected (see below).
- AU MIDI FX recognition, trigger-note chord generation, and downstream MIDI routing have been manually verified in Logic Pro on a clean second Mac. Source-derived vectors remain separate non-host evidence.
- ChordEngineCore remains independent of JUCE. HISE, HISE-generated code, and Projucer are not build dependencies.

## Repository layout

- `CMakeLists.txt` — C++17, pinned JUCE FetchContent dependency, product/formats/IDs, no automatic plugin copy, signing disabled for Apple development targets.
- `Source/Core/` — framework-independent scale/chord resolution, voicing, transpose, velocity, and event processing.
- `Source/MIDI/` — MIDI event model and allocation-free input tracker.
- `Source/State/` — in-memory configuration snapshot boundary; host persistence is deferred.
- `Source/Licensing/` — inert boundary; no credentials, endpoints, or invented trial rules.
- `Source/PluginProcessor.*`, `Source/PluginEditor.*` — JUCE processor and native scalable plug-in editor.
- `Tests/` — source-derived golden vectors plus core, MIDI tracker, processor integration, GUI state, and recorder/export regression tests.
- `Tools/GuiSnapshot.cpp` — development-only renderer that draws the real editor to PNG (800×650, 640×520, 1280×1040, live trigger, INFO page) for layout review without a DAW host. Built only with `-DBUILD_TESTING=ON`; never installed or shipped.
- `Docs/` — source-backed behavior inventory, requirements, test strategy, and SideChainer toolchain findings.
- `Resources/` — reserved for native resources; no legacy resource bundle copied.
- `Build/` — local CMake output; not source-controlled/release output.

## Build prerequisites

- CMake >= 3.22 (validated here with CMake 3.31.10). JUCE 9.0.3 is pinned because the installed Xcode 26 SDK fails to build JUCE 6.1.3's helper due to an API deprecated as unavailable in macOS 15.
- macOS: Xcode command-line tools/Xcode, Apple Clang, universal `arm64;x86_64` toolchains.
- Windows: Visual Studio 2022 C++ workload/MSVC, Windows SDK, and CMake; configure VST3 only.
- Internet access on first configure to fetch the pinned upstream JUCE commit. The fetched source is stored in CMake’s build tree; no global JUCE application or local HISE checkout is required.

## Reproducible macOS build (no install/sign/package)

CMake requires 3.22 or later for the pinned JUCE 9.0.3 source.

From the repository root:

```sh
cmake -S . -B Build/macOS-JUCE9 -G Xcode \
  -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON
cmake --build Build/macOS-JUCE9 --config Release --target ChordEngineAU_AU ChordEngineVST3_VST3
ctest --test-dir Build/macOS-JUCE9 -C Release --output-on-failure
```

Products are kept below `Build/macOS-JUCE9/ChordEngineAU_artefacts/Release/AU/ChordEngine.component` and `Build/macOS-JUCE9/ChordEngineVST3_artefacts/Release/VST3/ChordEngine.vst3`. Copy-after-build is explicitly disabled, certificate signing is disabled (the linker may still apply an ad-hoc signature), and there is no install/package step. Do not run `cmake --install`.

## Windows VST3 configure/build

Run on the Windows development machine from the repository root:

```powershell
cmake -S . -B Build/Windows -A x64 -DBUILD_TESTING=ON
cmake --build Build/Windows --config Release --target ChordEngineVST3_VST3
ctest --test-dir Build/Windows -C Release --output-on-failure
```

The CMake project adds only the VST3 target on Windows. Windows has not been built or host-tested by this macOS milestone.

## Native GUI and verification status

The native GUI is implemented in [Source/PluginEditor.cpp](Source/PluginEditor.cpp) with the processor/editor boundary in [Source/PluginProcessor.cpp](Source/PluginProcessor.cpp) and the account/trial/update subsystem in [Source/Licensing/](Source/Licensing). Start with [Docs/NATIVE_GUI_REFERENCE_INVENTORY.md](Docs/NATIVE_GUI_REFERENCE_INVENTORY.md), then use [Docs/SECOND_MAC_FUNCTIONAL_GUI_TEST.md](Docs/SECOND_MAC_FUNCTIONAL_GUI_TEST.md) as the current clean-second-Mac Logic checklist (it replaces `SECOND_MAC_GUI_TEST.md`, which remains the earlier milestone). GUI visual/control/resize, the real device-code sign-in and external recorder drag/drop still need manual host validation; see [Docs/BUILD_VERIFICATION.md](Docs/BUILD_VERIFICATION.md) for the final test results and evidence boundaries.

The original HISE source, the approved embedded FL 0.4.0 release source, and the native GUI are separate implementations. Use [Docs/REFERENCE_FUNCTIONAL_INVENTORY.md](Docs/REFERENCE_FUNCTIONAL_INVENTORY.md) and [Docs/FL_REFERENCE_BEHAVIOR_CAPTURE.md](Docs/FL_REFERENCE_BEHAVIOR_CAPTURE.md) for attribution and reference scope. Do not infer exact GUI parity from source inspection.

See [Docs/SIDECHAINER_BUILD_REFERENCE.md](Docs/SIDECHAINER_BUILD_REFERENCE.md) for the read-only JUCE/toolchain comparison and version rationale, and [Docs/BUILD_VERIFICATION.md](Docs/BUILD_VERIFICATION.md) for actual build/test/metadata results and verification limits.


## Functional parity pass (2026-10-05)

A functional pass replaced the remaining placeholders with real product
behaviour ported from the authoritative reference interface script
(`HISE Projects/ChordEngine-FL/Build/layout-info-card-authoritative-pristine-20261002.js`).
No reference, installed plugin, release package or SideChainer file was
modified.

- **Recorder.** The `RECORD ARM` / `CANCEL` / `STOP TAKE` pill is now visually
  contained by its own MIDI RECORDER card at every supported editor size,
  while keeping the reference pill coordinates. Arm, cancel, stop, the
  three newest takes, drag-and-drop export and MIDI export are unchanged and
  still covered by the 17 integration cases.
- **Fixed velocity.** The slider is interactive in `Fixed` mode and disabled in
  `Dynamic` / `Maximum`, exactly as the reference does it. Values 1, 64, 100
  and 127 update Core state and the generated MIDI velocity, and the numeric
  readout updates live.
- **Music-Prod logo.** The real 260 x 86 wordmark asset is now embedded and
  drawn 1:1 in the reference slot; the earlier substitute text wordmark is gone.
- **INFO page.** `CHECK FOR UPDATES`, `OPEN MUSIC-PROD STUDIO` and `SIGN IN`
  are real controls: an honest update check (there is no plugin-facing update
  API, so the status row says so and no version is invented), the
  `musicprodstudio://open` deep link, and the existing Music-Prod device-code
  sign-in flow. `SIGN OUT` shares the SIGN IN slot.
- **Trial.** A real 30:00 usage clock started by the first generated chord,
  shown live on the INFO page, with the reference `ACCESS EXPIRED` gate and
  the reference rule that an expired unlicensed session generates no chord.
- **Internal Audio.** Genuinely unavailable here: the reference bypasses its
  own `Sine Wave Generator1`, which needs an audio output bus, and this is an
  AU `aumi` / VST3 MIDI effect with no audio buses. The card keeps the
  reference geometry and caption but the pill is an explicitly **disabled**
  control with the reason shown and documented — never a fake ON/OFF.
- **Realtime safety.** Network and licensing work never runs on the audio
  thread; `processBlock` only reads one lock-free gate flag, and the auth
  service performs every request on a background thread pool and marshals the
  reply back to the message thread.

`CTest` is **6/6** (core, reference, 38/38 source-derived vectors, 17/17
processor integration, 18/18 functional, 16/16 update client). See
[Docs/BUILD_VERIFICATION.md](Docs/BUILD_VERIFICATION.md) and
[Docs/SECOND_MAC_FUNCTIONAL_GUI_TEST.md](Docs/SECOND_MAC_FUNCTIONAL_GUI_TEST.md).


## Functional fix pass (2026-10-06)

Focused correction pass for the four issues found in the first second-Mac
functional test. No reference, installed plugin, release package or SideChainer
file was modified; no commit, push, signing, notarization or packaging was done.

- **Fixed Velocity (issue 1).** The control is a real interactive
  `juce::Slider` (`LinearHorizontal`, 1..127 step 1). It is enabled only in
  `Fixed` mode and, crucially, it now *looks* disabled when it is disabled
  (`Dynamic`/`Maximum`), so it can never read as a live-looking static
  graphic. Dragging and clicking the track both work; the numeric readout, the
  Core state and the generated MIDI velocity all follow, and the value survives
  further MIDI processing. Core default is unchanged at Dynamic / 100.
- **Internal Audio (issue 2).** Decision: **no architecture change**. Internal
  audio needs an audio output bus; this product is an AU `aumi` MIDI FX and a
  VST3 MIDI effect with no audio buses, so there is nowhere to route a synth.
  Giving the VST3 an audio bus would stop it being a MIDI effect and change the
  plugin identity and host routing (and is impossible for the AU MIDI FX at
  all). The card therefore keeps the reference geometry and caption but its
  chip is a plain (non-button) status surface reading `N/A`, with
  `MIDI effect - no audio bus` on screen and the exact reason in a tooltip.
  An automated test pins the architecture: the MIDI-only bus layout is
  supported and a stereo output bus is rejected.
- **Clickable piano (issue 3).** The C2–C7 keyboard is now a real mini MIDI
  keyboard. Pressing a key pushes a raw note-on through a lock-free FIFO into
  `processBlock`, where it takes exactly the same Core path as an external
  MIDI note (`noteOn` → chord, velocity mode, transpose, recorder capture);
  releasing sends the matching note-off and releases the generated chord.
  Dragging between keys releases the old note and triggers the new one. The GUI
  contains **no** chord-generation code of its own — an automated test scans the
  editor source for that and compares an on-screen key against the same note
  played over MIDI.
- **Centered Record Arm (issue 4).** The `RECORD ARM` pill is now fully inside
  its own MIDI RECORDER card, horizontally centred on the card axis, with an
  even 3 px clearance above (below the last take row) and below (inside the
  card's bottom edge). The geometry lives in named class constants shared by
  `paint()`, `resized()` and the layout test, and a test asserts containment,
  centring and inset at 800x650, 640x520 and 1280x1040. Arm/cancel/stop and
  take drag-export are unchanged.

`CTest` is **6/6** at this revision (core, reference, 38/38 source-derived
vectors, 17/17 processor integration, 18/18 functional, 16/16 update client). Development test
archives: [AU](../Build/TestArtifacts/ChordEngine-Native-macOS-AU-Functional-Fix-Test.zip)
SHA-256 `c303e154a0a7ebc2595ac3c06182451eb85ee9395d7efc8d591e8f393dc52509` and
[VST3](../Build/TestArtifacts/ChordEngine-Native-macOS-VST3-Functional-Fix-Test.zip)
SHA-256 `91c3d0d78d8572aa010b4d7c43adafc0ad34f960d4e3ec99801fb0b6f65a9f88`.


## Native update service (2026-10-06)

The plugin now checks for updates against the EXISTING Music-Prod release
service, anonymously, from a self-contained module in `Source/Updates/` that is
**separate from licensing** (see [Docs/UPDATE_SERVICE.md](Docs/UPDATE_SERVICE.md)).

- **Real endpoint, strict validation.** One anonymous
  `GET …/functions/v1/music-prod-studio-api/updates?product=chordengine&current=<build>&platform=<os>`.
  Every field is validated (success flag, product slug, both versions as
  semver, platform match, decision and internal consistency) and an
  inconsistent payload is refused rather than half-read.
- **Honest states.** Checking, up to date, update available, mandatory update,
  no published release, offline (with or without a cached answer) and error.
  The live `chordengine` answer (`status: noPublishedRelease`, `latestVersion:
  null`, `decision: upToDate`) renders as *No published release* on both the
  Latest and Status rows — it is never shown as "Up to date" and no version is
  ever fabricated.
- **Actual build version.** What the plugin sends and what the UI shows both
  come from the build metadata (`JucePlugin_VersionString`, `0.1.0` in this
  development build); `0.4.0` is not hardcoded anywhere in the client.
- **Threading.** Exactly one background worker (`juce::ThreadPool { 1 }`),
  results delivered with `MessageManager::callAsync`, the audio/MIDI thread
  never touches the module, and `processBlock` is asserted to contain no update
  code. Checks run once on editor open (after the cached answer is shown) and on
  an explicit `CHECK FOR UPDATES` click, with at most one check in flight.
- **Timeouts and retry.** 5 s connect timeout plus a 5 s read deadline; the
  only retried failure is HTTP 503 / `releaseBackendUnavailable`, retried once.
  A failure keeps the last successful answer and is never reported as success.
- **Cache.** `update-cache.json` next to the plugin's other Music-Prod state,
  holding only the normalised public answer fields and a timestamp — no account,
  session, licence or usage data. On editor open it is shown at once (marked
  `(cached)`) and then refreshed exactly once per launch.
- **No download.** A successful check never opens Music-Prod Studio, never
  downloads and never calls `/downloads/authorize`; installing an update stays
  with Music-Prod Studio.

`CTest` is **6/6** at this revision (core, reference, 38/38 source-derived
vectors, 17/17 processor integration, 18/18 functional, 16/16 update client).
The update transport is injected in tests and the shipped plugin's own object
files are checked to reference only the real HTTP transport, so no automated
run touches the live service.
