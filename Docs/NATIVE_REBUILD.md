# ChordEngine 0.6.0: the JUCE-free native rebuild

Branch `juce-free` (from `main` at `119e509`, ChordEngine 0.5.1). Status as of
2026-10-22: builds and passes every automated test on macOS (universal) and
on the Windows build PC (VST3). **Not signed, notarized, packaged for release,
published, installed or host-tested in a DAW.** Releases stay with the owner.

ChordEngine no longer contains or links any JUCE code. The only third-party
code is:

| SDK | Version | Licence | Used for |
|---|---|---|---|
| Apple AudioUnitSDK | 1.4.0, tag `AudioUnitSDK-1.4.0`, commit `bd98b31feff57a15989fcfab4cd86dc63382b1ac` | Apache License 2.0 | the two Audio Units |
| Steinberg VST 3 SDK | 3.8.0, tag `v3.8.0_build_66`, commit `9fad9770f2ae8542ab1a548a68c1ad1ac690abe0` (submodules `pluginterfaces` `31d6eeba…`, `base` `3d2e82f8…`, `public.sdk` `a3911a46…`) | MIT License | the VST3 |

Both are vendored unmodified under `ThirdParty/` (provenance in
`ThirdParty/README.md`). The licence texts and attributions are in
`Docs/THIRD_PARTY_NOTICES.txt`, which is copied into every bundle's
`Contents/Resources/` and into the DMG. The editor's fonts (Inter, Schibsted
Grotesk) are SIL OFL 1.1 as before. "VST" naming/logos follow Steinberg's VST 3
Usage Guidelines, which are separate from the code licence.

## Build, test, package

From the repository root (CMake ≥ 3.22, Xcode with C++23 support for the
AudioUnitSDK; the first `Tools/FetchSdk.sh` run needs network access):

```sh
Tools/FetchSdk.sh        # full VST 3 SDK + Steinberg validator into Build/sdk-src (test tools only)
cmake -S . -B Build/native -G "Unix Makefiles" \
  -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build Build/native -j 8
ctest --test-dir Build/native --output-on-failure

# sanitizers (arm64 Debug)
cmake -S . -B Build/native-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_OSX_ARCHITECTURES=arm64 -DBUILD_TESTING=ON -DCHORDENGINE_ASAN=ON
cmake -S . -B Build/native-tsan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_OSX_ARCHITECTURES=arm64 -DBUILD_TESTING=ON -DCHORDENGINE_TSAN=ON

# editor screenshots without a host (PNG, offscreen)
Build/native/ChordEngineEditorSnapshot <out-dir> [<0.5.1 dir> <compare dir>]

# installer structure check (NO signing/notarization; not for distribution)
Tools/PackageMacDMG.sh --unsigned --force
```

Bundles land in `Build/native/Bundles/`. They are sealed ad hoc only.
Tests that touch user folders run with `CFFIXED_USER_HOME` pointing at a
temporary folder (setting `HOME` does not redirect `NSSearchPathForDirectories`),
so test runs never read or write the real licence/pack/update files.

Windows (build PC, VS 2022, from the repo root):

```powershell
cmake -S . -B Build/Windows -A x64 -DBUILD_TESTING=ON
cmake --build Build/Windows --config Release --target ChordEngineVST3
ctest --test-dir Build/Windows -C Release --output-on-failure
```

The VST3 bundle is `Build/Windows/Bundles/ChordEngine.vst3`
(`Contents/x86_64-win/ChordEngine.vst3` + `Contents/Resources/moduleinfo.json`).
`.github/workflows/windows-vst3.yml` builds and tests it on pushes to `main`
and `juce-free`.

## Identity: what stays, what changes

Kept exactly as 0.5.1, so existing Logic, FL Studio and other projects find
the plug-in:

| | MIDI FX AU | Instrument AU | VST3 |
|---|---|---|---|
| type / subtype / manufacturer | `aumi` `Cefl` `Musc` | `aumu` `Cein` `Musc` | class ID `ABCDEF019182FAEB4D7573634365666C` |
| name | `Music-Prod: ChordEngine` | `Music-Prod: ChordEngine Instrument` | `ChordEngine`, vendor `Music-Prod`, category `Instrument` |
| factory / entry | `ChordEngineAUFactory` | `ChordEngine_InstrumentAUFactory` | `GetPluginFactory`, `bundleEntry`, `bundleExit` |
| bundle file | `ChordEngine.component` | `ChordEngine Instrument.component` | `ChordEngine.vst3` |
| parameters | — | — | bypass `'byps'` (0x62797073); MIDI CC parameters `0x6d636d00 + channel*130 + cc` via `IMidiMapping` |

New, unique bundle identifiers (0.5.1 used `com.musicprod.chordengine`,
`com.musicprod.chordengine.instrument` and `com.musicprod.chordenginefl`):

| Bundle | 0.6.0 `CFBundleIdentifier` |
|---|---|
| `ChordEngine.component` (MIDI FX) | `com.musicprod.chordengine.au` |
| `ChordEngine Instrument.component` | `com.musicprod.chordengine.instrument.au` |
| `ChordEngine.vst3` | `com.musicprod.chordengine.vst3` |

Music-Prod Studio (PR #14) must learn these ids: it identifies installed
ChordEngine bundles by bundle id, and 0.5.x copies keep the old ones. Hosts
identify AUs by type/subtype/manufacturer and VST3s by class ID, so the bundle
id change does not affect projects.

AU version = `major<<16 | minor<<8 | patch` (0.6.0 → 1536). Every bundle has
`Contents/PkgInfo` (`BNDL????`), `LSMinimumSystemVersion` 12.0, universal
arm64 + x86_64 binaries built for macOS 12.0, and exports only its entry
points. `Tools/CheckBundles.sh` checks all of it (CTest `ChordEngineBundleMetadata`).

The installer (`Tools/PackageMacDMG.sh`) builds the pkg with a component plist
that sets `BundleIsRelocatable=false` for all three bundles and fails if
`PackageInfo` relocates anything, so an update is always written to
`/Library/Audio/Plug-Ins/...` and never redirected into an older copy elsewhere.

## Architecture

```
Source/Core, Source/MIDI/MidiNoteTracker, TakeQuantize   unchanged from 0.5.1
Source/Platform/      our OS layer: JSON, text, bytes, time, CSPRNG, SHA-256, HTTP
                      (NSURLSession / WinHTTP), main-thread dispatch, worker queue,
                      main-thread timers, app-data folder, atomic file writes
Source/State, Licensing, Updates, Packs, Audio           ported from JUCE types to std + Platform
Source/PluginProcessor.*                                 format-independent processor
Source/AU/ChordEngineAU.mm                               AudioUnitSDK adapter, compiled twice
                                                         (MIDI FX / instrument)
Source/VST3/ChordEngineVST3.mm                           single-component VST3 adapter (mac + Windows)
Source/UI/                                               AppKit editor (NSView), see Docs/UI_NATIVE_EDITOR.md
```

### Ported (same behaviour, JUCE types replaced)

- Chord engine (`Source/Core/*`, `MidiNoteTracker`, `TakeQuantize`): untouched.
- Licensing (`Source/Licensing/*`): device-code sign-in, licence-key
  activation, 30-minute trial clock, lapse to demo. The backend calls are the
  same requests as 0.5.1: same endpoints, JSON fields, `Content-Type:
  application/json`, 12 s timeout; `juce::var` semantics are reproduced by
  small helpers. The contract, route-switch and runtime suites (now 3 suites)
  pass unchanged in substance.
- Updates (`Source/Updates/*`), packs (`Source/Packs/*`): same protocol and
  file layout; HTTP via `platform::httpRequest`, files via `std::filesystem`.
- State (`Source/State/*`): same `<CHORDENGINE_STATE …/>` XML, written by a
  small XML reader/writer (`XmlLite`) with the attribute order 0.5.1 used.
- Internal synth (`Source/Audio/InternalSynth.*`): same arithmetic
  (`LinearSmoothed` mirrors JUCE's smoother), denormal flushing, same sounds.
- Recorder and MIDI file export: `MidiFileWriter` writes byte-identical SMF
  (format 1, one track, tempo + time signature meta, running status).

### Rewritten

- Processor host interface (`PluginProcessor.*`): no `AudioProcessor`; the
  adapters pass an `AudioBlock`, a fixed-capacity `MidiEventList` and a
  `HostTransport`. Audio-thread work is allocation- and lock-free.
- AU adapter on `ausdk::MusicDeviceBase`: channel configs `{0,1}` and `{0,2}`
  (anything else refused in `ValidFormat`/`Initialize`), output bus named
  "Output", MIDI out via the host's MIDI event list block when installed,
  otherwise the legacy output callback (never both: that caused duplicate
  notes), state under ClassInfo key `jucePluginState`, one factory preset
  "Default", Cocoa view factory.
- VST3 adapter: one component implementing `IAudioProcessor`,
  `IEditController`, `IMidiMapping`; buses "Output" (mono/stereo, others
  refused), "MIDI Input", "MIDI Output"; host bypass persisted in state
  (`hostBypass`), which the validator's bypass-persistence test needs.
- Editor: AppKit `NSView`, same look as `Docs/UI_REDESIGN_0.5.0.md`, every
  control real (details and open items in `Docs/UI_NATIVE_EDITOR.md`).
  Take drag-out uses `NSFilePromiseProvider`/file URLs.
- Windows platform layer (`PlatformWin.cpp`): WinHTTP, BCrypt, a message-only
  dispatcher window for main-thread jobs and timers.

## Compatibility with 0.5.x projects

`Tests/Fixtures/State051/` holds state captured from the real, installed 0.5.1
binaries (`Tools/StateCapture/CaptureLegacyState.mm`, see the fixture README):
AU MIDI FX + instrument `.aupreset` and ClassInfo blobs (default and a custom
state with every field changed) and the VST3 component state, including the
JUCE-era private-data trailer.

- `ChordEngineStateTests` (case H): every 0.5.1 blob restores every field;
  re-saving produces the same attributes in the same order with the same
  values, so 0.5.1 can also read 0.6.0 state.
- `ChordEngineAUValidateMidiFx` / `…Instrument` (in-process `AUProbe`): the
  0.5.1 `.aupreset` files restore all fields through `kAudioUnitProperty_ClassInfo`;
  ClassInfo round trip; channel configs (mono/stereo yes, 4 channels refused);
  a note produces the chord on the MIDI output.
- `ChordEngineVST3HostTest`: the exact bytes 0.5.1's VST3 wrote (with the
  trailer) restore every field; the default state restores defaults; re-saved
  state reloads unchanged; parameter IDs and `IMidiMapping` match 0.5.1.

Not yet done: opening a real saved Logic/FL Studio project in the DAW (needs
the build installed; see open items).

## Test results (2026-10-22)

| Suite | macOS Release universal | ASan+UBSan | TSan | Windows |
|---|---|---|---|---|
| 13 unit/integration suites (core, FL reference vectors, internal synth, state, licensing ×3, updates, packs, processor integration, functional, RT safety) | pass | pass | pass | pass (13/13 CTest incl. NoJuce) |
| EditorTests (72 checks) | pass | — | — | n/a |
| RealtimeSafety: 256 blocks, 0 allocations / 0 frees / 0 locks (positive control trips the probe) | pass | | | |
| NoJuce: sources + both architectures of all 3 bundles: 0 JUCE symbols, system libraries only (negative control: 0.5.1 AU has 23,478 / 21,682 JUCE symbols) | pass | | | pass (sources; DLL imports only Windows/MSVC runtime DLLs, exports only `GetPluginFactory`/`InitDll`/`ExitDll`, no JUCE strings besides the two state keys) |
| AU probe MIDI FX + Instrument | pass | | | |
| VST3 host test (identity, buses, params, 0.5.1 state, chord out, RT-safe `process()`) | pass | | | |
| Steinberg validator 3.8.0_build_66 | 47 passed, 0 failed | | | 47 passed, 0 failed |
| Bundle metadata (ids, versions, PkgInfo, AU version, exports, notices, strict seal) | pass | | | |
| Unsigned pkg/DMG structure check (3 bundles, non-relocatable, BOM 0755 root:wheel) | pass | | | |

`auval`: Apple's `auval -v aumi Cefl Musc` / `auval -v aumu Cein Musc` only
see registered (installed) components. Installing into `/Library/Audio/Plug-Ins`
or `~/Library/Audio/Plug-Ins` on this Mac is not allowed, and the installed
0.5.1 already owns these codes here, so `auval` would test 0.5.1. The
in-process probe (`Tests/AUProbe.mm`, `AudioComponentRegister` on the built
bundle) covers the same ground for the format contract; the real `auval` run
belongs to the host-test step below.

## Screenshots

`Docs/Screenshots/0.5.1/` (before, rendered by the 0.5.1 snapshot tool),
`Docs/Screenshots/native/` (after, `ChordEngineEditorSnapshot`) and
`Docs/Screenshots/compare/` (side by side + difference), 17 states each
(CHORD/INFO/packs/recorder/armed/live/internal audio/activation at 640×520,
800×650 and 1280×1040). Mean pixel difference 0.37–0.62 (of 255); 0.25–1.0 %
of pixels differ by more than 24, from the version string, the new resize grip,
text rasterisation and the activation image's pressed state.

## Windows editor plan

The Windows VST3 currently has no editor (`createView` supports only
`kPlatformTypeNSView`, so hosts show their generic parameter view). Plan:

1. Split `Source/UI/EditorWidgets.mm`/`EditorView.mm` into a platform-neutral
   C++ layer (layout in the 800×650 design space, hit testing, widget state,
   a paint list of rects/rounded rects/lines/text runs/images) and thin
   platform back ends. `EditorStyle` colours/metrics move with it.
2. Windows back end: `IPlugView` for `kPlatformTypeHWND`, a child `HWND`,
   Direct2D + DirectWrite for drawing (Inter/Schibsted Grotesk loaded
   privately with `AddFontMemResourceEx`/DirectWrite custom font collection),
   per-monitor DPI awareness, `IPlugViewContentScaleSupport`, resize via
   `IPlugFrame::resizeView` within 640×520–1280×1040.
3. Take drag-out with OLE `DoDragDrop` and a `CF_HDROP` of the exported `.mid`.
4. Snapshot tool renders through Direct2D to WIC PNGs, compared against the
   macOS snapshots in CI on the build PC.

## Open items

- Real-host checks on the test Mac / second Mac: install the 0.6.0 pkg
  (owner-approved), run `auval -v aumi Cefl Musc` and `auval -v aumu Cein Musc`,
  open saved 0.5.1 Logic and FL Studio projects, check MIDI drag-and-drop into
  the DAW. The test Mac still has 0.5.0 duplicates in `~/Library/Audio/Plug-Ins`
  with the same AU codes; they would shadow or confuse the result and should
  be moved away first (owner decision pending).
- Music-Prod Studio must learn the three new bundle ids before it can update
  0.6.0 installs.
- Editor: SIGN IN is not clicked in tests (needs an auth-service seam in the
  processor), keyboard focus/navigation not ported, selector popups are native
  menus; Windows editor (plan above).
- Release packaging: `Tools/PackageMacDMG.sh` (without `--unsigned`) signs,
  notarizes and staples; only the owner runs it.
- `Docs/MPF_REQUESTS.md`: what ChordEngine would want from the Music-Prod
  Plugin Framework so the shared parts live there.
