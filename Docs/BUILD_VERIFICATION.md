# Build verification history and final GUI milestone

## macOS installer DMG milestone — Developer ID signed + notarized (2026-10-06, latest)

The universal Release AU and VST3 artefacts were packaged into a single macOS
installer and shipped inside a signed, notarized, stapled disk image. Both
plugin formats are installed by one package:

* `Tools/PackageMacDMG.sh` — the packaging pipeline (staging, code signing,
  `pkgbuild`, `productsign`, `notarytool`, `stapler`, `hdiutil`). It never
  modifies the build tree: the bundles are copied with `ditto` into a temporary
  staging root and signed there, so
  `Build/macOS-JUCE9/**` keeps the ad-hoc signature Xcode produces.
* Produced with `./Tools/PackageMacDMG.sh` (no flags).

Deliverables (in the git-ignored `Build/ReleaseArtifacts/`):

| Artifact | Size (bytes) | SHA-256 |
| --- | --- | --- |
| `ChordEngine-0.1.0-macOS.dmg` | 16,728,417 | `8b722e3c1fec2dcf70c9104b285ceb0506a7fcdfb3504648eb539279088d4d65` |
| `ChordEngine-0.1.0-macOS.pkg` | 15,680,150 | `bc8c205d158ffe1ced8e0731b6cb3fadbebaa0abccc37e398e510aeacb3ccee8` |

Package identifier `com.musicprod.chordengine.pkg`, install location `/`, so the
payload lands at
`/Library/Audio/Plug-Ins/Components/ChordEngine.component` (AU) and
`/Library/Audio/Plug-Ins/VST3/ChordEngine.vst3` (VST3).

Verified independently of the script's own gates:

* `codesign -dv --verbose=4` on both payload bundles: `Authority=Developer ID
  Application: Martin Kadziolka (3A4R5EKM7V)`, `TeamIdentifier=3A4R5EKM7V`,
  `flags=0x10000(runtime)`, trusted timestamp, `codesign --verify --deep
  --strict` clean.
* `pkgutil --check-signature`: signed by a developer certificate issued by
  Apple for distribution, **and** trusted by the Apple notary service.
* `spctl --assess --type install`: `accepted`, `source=Notarized Developer ID`.
* `xcrun stapler validate`: valid on both the `.pkg` and the `.dmg`.
* Mounted DMG contains the stapled `.pkg` plus `README.txt`; the payload bucket
  hash equals the published `.pkg` hash.
* BOM modes: every directory archived `40755 / uid 0 / gid wheel`, executables
  `100755`, **no** `0700` entries.
* Payload is universal (`x86_64 arm64`) for both formats.

Unchanged by this work (re-verified): AU still `aumi`/`Abcd` version `0.1.0`,
VST3 class still `Fx`, no audio buses added. The bundles in
`Build/macOS-JUCE9/**` are still `Signature=adhoc`, and the installed
`~/Library/Audio/Plug-Ins/{VST3,Components}/ChordEngine.*` plus the ChordEngine
FL VST3 (`3ffbe1f3…`) and `Binaries/Source/Plugin.cpp` (`1044c271…`) are
byte-identical to before. Notarization used the existing `ChordEngineFL-notary`
keychain profile; no credentials are stored in the repository.

Known caveat: the native VST3 declares `BUNDLE_ID com.musicprod.chordenginefl`
and plugin code `Cefl` (shared with the ChordEngine FL build), so the two share a
VST3 class ID. That identity was deliberately left unchanged, but a host that
has both installed may treat them as the same plugin.

## Update service milestone verification (2026-10-06)

The native update client (`Source/Updates/`, see
[Docs/UPDATE_SERVICE.md](UPDATE_SERVICE.md)) was added and the whole project was
reconfigured and rebuilt with the pinned JUCE 9.0.3 for macOS universal
`arm64;x86_64` Release. The build after the last source edit finished with
**0 errors** and no new warnings in the changed files.

* CTest: **6/6 passed** — the previous five targets plus `ChordEngineUpdateTests`.
* Direct source-derived vectors: **38/38 matched**, `hostObserved=false`, `unresolved=0`.
* Direct processor integration: **17/17 cases passed**.
* Direct functional suite: **18/18 cases passed** (all 18 cases retained).
* Direct update suite: **16/16 cases passed** — deterministic, no live network.
* Repeat runs (3×) of the update and functional suites: stable, no flakes.
* No existing fixture, assertion or expected value was weakened; the only
  existing expectation that changed is the INFO page's *Current version* row,
  which now reports the actual build version (`0.1.0`) instead of the reference
  product's `0.4.0`, exactly as the milestone requires. The licensing
  vocabulary, the account bridge and the model-level update tests are unchanged.

What the 16 update cases prove (see `Tests/UpdateTests.cpp`): the live
`noPublishedRelease` payload is never reported as "Up to date"; up-to-date,
optional and mandatory answers map to the reference rows and wording; the
strict validator refuses an invalid current version, an invalid latest version,
a wrong product, a wrong platform, an unsuccessful envelope, an unknown
decision and an internally inconsistent payload; only HTTP 503 / the server's
`releaseBackendUnavailable` code is retried, exactly once; offline keeps the
last successful answer marked `(cached)`; the cache round-trips with no
account-, session- or licence-related content; the published states run
`cached → checking → answer`; a check sends only the product, the build version
and the platform; `processBlock` (and the processor) contains no update code at
all; the client owns exactly one background worker however often it is asked;
and the version sent and displayed is the actual build version.

Artifact-level proof that the shipped plugin uses the real transport while no
dev/test build touches the network:

| Object file | `HttpUpdateTransport` refs | `OfflineUpdateTransport` refs |
| --- | --- | --- |
| `ChordEngineAU` `PluginEditor.o` | 1 | 0 |
| `ChordEngineVST3` `PluginEditor.o` | 1 | 0 |
| `ChordEngineGuiSnapshot` `PluginEditor.o` | 0 | 1 |

The AU and VST3 shared-code compile commands both carry
`JucePlugin_VersionString="0.1.0"`, so the version the plugin sends and
displays comes from the build metadata of the binary that was actually built.
The `0.0.0` fallback string is absent from both shipped binaries, which contain
the `functions/v1/music-prod-studio-api/updates` endpoint and the
`?product=`, `&current=`, `&platform=` query fragments.

Protected artifacts re-verified after this milestone: the approved FL VST3
SHA-256 is still `3ffbe1f349846df64dc7e2cf3357b1e682e4f8f1dd3cf8a879080edc773483ed`
at both locations, the FL `Binaries/Source/Plugin.cpp` MD5 is still
`1044c2719acb8d49573c5aadd9e7ca22`, the installed VST3/AU components are
unchanged, and `find … -newermt '2026-10-06 12:28'` reports **0** modified files
in `HISE Projects/ChordEngine`, `HISE Projects/ChordEngine-FL` and `SideChain`.
No signing, notarisation, packaging or publishing was performed.

## Functional fix pass verification (2026-10-06)

Focused correction pass for the four issues reported after the first
second-Mac functional test. Reconfigured and rebuilt Xcode Release for
universal `arm64;x86_64` with pinned JUCE 9.0.3; the build after the last
source edit contained no errors and no new warnings in the changed files
(the only warnings emitted are the pre-existing `-Wfloat-equal` notes in
`Tests/FunctionalTests.cpp` trial-clock comparisons).

- CTest: **5/5 passed** (`ChordEngineCoreTests`, `ChordEngineReferenceTests`,
  `ChordEngineFLReferenceVectorTests`, `ChordEngineProcessorIntegrationTests`,
  `ChordEngineFunctionalTests`).
- Direct source-derived vector suite: **38/38 matched**, `hostObserved=false`,
  `unresolved=0`.
- Direct processor integration suite: **17/17 cases passed**.
- Direct functional suite: **18/18 cases passed** (the previous 14 unchanged,
  plus 4 new cases). No existing assertion, fixture or expected value was
  weakened or removed.

What the four new cases prove:

15. **Fixed velocity is a real mouse control.** The control is asserted to be
    a `juce::Slider` (not a painted graphic) in `LinearHorizontal` style with
    range 1..127/interval 1; it is disabled *and visibly dimmed* in Dynamic and
    fully opaque in Fixed; and a genuine press-and-drag through the slider's
    own `mouseDown`/`mouseDrag`/`mouseUp` handlers — driven by a real
    `juce::MouseEvent` built from `Desktop::getMainMouseSource()`, not a
    `setValue` shortcut — moves the value, updates the Core, updates the
    numeric readout, and changes the velocity of the *generated MIDI* at
    1, 64, 100 and 127. A click low on the track also moves the value.
16. **The C2–C7 keyboard is a real MIDI input surface.** The shared key
    geometry is mapped through the same hit test the painting uses: the white
    row resolves to the 36 naturals C2..C7 ascending, and the full key bed
    resolves 1:1 to MIDI notes 36..96 in order. A `mouseDown` on C3 reaches
    the Core as a real trigger note, generates a chord, and produces exactly
    the same pitches and velocities as the same note played through the
    external MIDI path (compared against a second processor instance). A
    drag from C3 to E3 releases the first trigger and triggers the second;
    `mouseUp` releases the trigger and every generated chord note and emits
    real note-offs. The editor source is also scanned to prove it contains no
    chord-resolution call of its own (no `generateChordForTrigger`) and does
    route keys through `pushUiNoteOn`.
17. **RECORD ARM containment and centring.** At 800x650, 640x520 and
    1280x1040 the pill's bounds are asserted to be fully inside the same
    design-mapped recorder card rectangle the painting uses, horizontally
    centred on it (within 1 px), never touching the card boundary, and to keep
    a visible inset. Arm/cancel behaviour is re-checked through the real
    button afterwards.
18. **INTERNAL AUDIO is honestly unavailable.** The chip is asserted to be
    **not** a `Button` (so it cannot fake a toggle) with no clickable control
    published; its tooltip and the on-screen caption state the limitation.
    The architecture itself is pinned: `isMidiEffect()` is true, the empty
    (MIDI-only) bus layout is supported, and a stereo output bus is rejected.

Artifacts (development test bundles, not releases):

- [AU Functional Fix test ZIP](../Build/TestArtifacts/ChordEngine-Native-macOS-AU-Functional-Fix-Test.zip),
  SHA-256 `c303e154a0a7ebc2595ac3c06182451eb85ee9395d7efc8d591e8f393dc52509`.
- [VST3 Functional Fix test ZIP](../Build/TestArtifacts/ChordEngine-Native-macOS-VST3-Functional-Fix-Test.zip),
  SHA-256 `91c3d0d78d8572aa010b4d7c43adafc0ad34f960d4e3ec99801fb0b6f65a9f88`.

Both extract to a single root bundle, pass `unzip -t`, are universal
(`x86_64 arm64` via `lipo -info`), carry only an ad-hoc local signature, and
keep the AU identity unchanged (`type = aumi`, `subtype = Abcd`,
`manufacturer = Abcd`, `factoryFunction = ChordEngineAUFactory`,
`name = "Music-Prod: ChordEngine"`).

Automated tests cannot replace the manual host checks; the updated checklist
is [SECOND_MAC_FUNCTIONAL_GUI_TEST.md](SECOND_MAC_FUNCTIONAL_GUI_TEST.md)
(sections A-E, a **new disposable Logic project only**). GUI rendering,
mouse interaction, host routing and Help launch still require that manual pass.

Protected artifacts re-checked read-only after this build: approved FL VST3
executable SHA-256 `3ffbe1f349846df64dc7e2cf3357b1e682e4f8f1dd3cf8a879080edc773483ed`
(verified at `HISE Projects/ChordEngine-FL/Build/ChordEngine FL.vst3/Contents/MacOS/ChordEngine FL`
and its release copy) and `Binaries/Source/Plugin.cpp` MD5
`1044c2719acb8d49573c5aadd9e7ca22`; both unchanged. No file under
`HISE Projects/ChordEngine` or `HISE Projects/ChordEngine-FL` has an mtime
on/after 2026-10-06, so nothing outside
`/Users/martin/Documents/ChordEngine-Native` was modified during this pass.

## GUI reference-parity pass verification (2026-10-05, earlier pass)

Reconfigured Xcode Release for universal `arm64;x86_64` with pinned JUCE 9.0.3 and rebuilt the AU, VST3, test, and development snapshot targets from the rewritten editor. The final build after the last source edit contained no compiler errors or warnings.

- CTest: **4/4 passed** (`ChordEngineCoreTests`, `ChordEngineReferenceTests`, `ChordEngineFLReferenceVectorTests`, `ChordEngineProcessorIntegrationTests`).
- Direct processor integration suite: **17/17 cases passed** (9/9 source-derived comparisons inside it).
- Direct source-derived vector suite: **38/38 matched**, `hostObserved=false`, `unresolved=0`. No fixture, expected value, or assertion was changed for this pass.
- Visual evidence: the development-only `ChordEngineGuiSnapshot` tool rendered the real editor through the real processor path to `Build/macOS-JUCE9/Release/GuiSnapshots/` at 800×650, 640×520, 1280×1040, plus a live-trigger and an INFO-page capture. The rendering shows the reference hierarchy, the reference terminology, correct trigger/generated piano highlighting, and **no malformed characters**.
- New development archives (development test bundles, not releases): [AU GUI parity ZIP](../Build/TestArtifacts/ChordEngine-Native-macOS-AU-GUI-Parity-Test.zip) SHA-256 `7d366b49ddfa1b4d727e1e352250a81aa7845da753c6911eaca0834cb550e8ba`, [VST3 GUI parity ZIP](../Build/TestArtifacts/ChordEngine-Native-macOS-VST3-GUI-Parity-Test.zip) SHA-256 `0fe46180be6b2fb741af0ddc05b14bae8c35e8c5a0670e92bc99389f63058d29`. Each passes `unzip -t`, has one expected root bundle, and carries universal `x86_64 arm64` executables whose hashes match the built bundles.
- Protected artifacts re-checked read-only: approved FL VST3 executable SHA-256 `3ffbe1f349846df64dc7e2cf3357b1e682e4f8f1dd3cf8a879080edc773483ed` (verified at `HISE Projects/ChordEngine-FL/Build/Release/ChordEngine-FL-0.4.0/Mac/ChordEngine FL.vst3/Contents/MacOS/ChordEngine FL`) and `Binaries/Source/Plugin.cpp` MD5 `1044c2719acb8d49573c5aadd9e7ca22`; both unchanged. Nothing outside `/Users/martin/Documents/ChordEngine-Native` was modified during this pass.

Manual checks that still have to happen on the clean second Mac are listed in [SECOND_MAC_GUI_PARITY_TEST.md](SECOND_MAC_GUI_PARITY_TEST.md). Runtime host rendering and interaction are not verified by the snapshot tool, which renders the editor outside a DAW.

## Final native GUI milestone verification (2026-10-05, earlier pass)

Configured Xcode Release for universal `arm64;x86_64` using pinned JUCE 9.0.3. The final build compiled the AU, VST3, and test targets successfully. No DAW automation or development-Mac plugin installation was performed.

Verification completed after the last source/test edits:

- CTest: **4/4 passed** (`ChordEngineCoreTests`, `ChordEngineReferenceTests`, `ChordEngineFLReferenceVectorTests`, `ChordEngineProcessorIntegrationTests`).
- Direct processor suite: **17/17 cases passed**, including configuration effects on real next-block MIDI, generated-only recorder capture, MIDI file timing/closed notes, cancellation, invalid take index, and newest-first rolling three-take export.
- Direct source-derived vector suite: **38/38 matched**; remains `hostObserved: false`.
- Final development ZIPs recreated from built AU and VST3 bundles; each has one expected root bundle and passes archive integrity tests. Both executable slices are `arm64` and `x86_64`.

Artifacts and exact hashes are listed in [MANIFEST.txt](../Build/TestArtifacts/MANIFEST.txt):

- [AU GUI test ZIP](../Build/TestArtifacts/ChordEngine-Native-macOS-AU-GUI-Test.zip), SHA-256 `5ede6595b034fccdf260d82583880e92caf3a22ac4d8ac9da8763d954e4805ac`.
- [VST3 GUI test ZIP](../Build/TestArtifacts/ChordEngine-Native-macOS-VST3-GUI-Test.zip), SHA-256 `c414014ebf3bd9a740f8a448e324a201a91faed2b77231a758740e80945435a7`.

Martin's manual clean-second-Mac Logic test separately confirmed native AU MIDI FX recognition, MIDI input, expected chord generation for a valid trigger, and downstream Classic Electric Piano MIDI routing. This is recorded in [HostObservations.json](../Tests/HostObservations.json) with `hostObserved: true`; the source-derived fixture remains `hostObserved: false`. It is not a DAW automation test and does not verify GUI rendering or interactions.

A separate manual GUI checklist is [SECOND_MAC_GUI_TEST.md](SECOND_MAC_GUI_TEST.md). Logic GUI display/controls/resizing, Help launch, and recorder external file drop remain pending manual verification. Trial/licensing/account behavior and internal audio remain placeholders/out of scope. Host state persistence is deferred.

Protected artifact checks were run read-only after changes:

- Approved FL VST3 (`Build/Release/ChordEngine-FL-0.4.0/Mac/ChordEngine FL.vst3/Contents/MacOS/ChordEngine`) SHA-256 remains `3ffbe1f349846df64dc7e2cf3357b1e682e4f8f1dd3cf8a879080edc773483ed`.
- Protected ChordEngine-FL `Binaries/Source/Plugin.cpp` MD5 remains `1044c2719acb8d49573c5aadd9e7ca22`.
- No protected HISE/FL projects, SideChainer, installed plugins, or existing release artifacts were modified. Task source/docs/build changes are confined to `/Users/martin/Documents/ChordEngine-Native`; read-only timestamps/hashes of installed ChordEngine plugin executables predate this milestone build.

No release packaging, distribution signing, notarization, publishing, or automatic installation was performed. Xcode/CMake may use local ad-hoc signatures as build tooling behavior; the resulting GUI ZIPs are explicitly development test artifacts only.

---

## Functional parity pass (2026-10-05)

Second complete configure + universal Release build + CTest run, after the
functional-pass source and test edits.

### Commands

```sh
"$HOME/Library/Python/3.9/bin/cmake" -S . -B Build/macOS-JUCE9 -G Xcode \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' \
  -DBUILD_TESTING=ON -DCHORDENGINE_FETCH_JUCE=ON
cmake --build Build/macOS-JUCE9 --config Release --parallel 4
"$HOME/Library/Python/3.9/bin/ctest" --test-dir Build/macOS-JUCE9 -C Release --output-on-failure
```

### Result

- Configure: success (pinned JUCE 9.0.3 via FetchContent).
- Build: success for AU, VST3, all four test executables, and the development
  GUI snapshot tool.
- **CTest: 5/5 passed** — `ChordEngineCoreTests`,
  `ChordEngineReferenceTests`, `ChordEngineFLReferenceVectorTests`,
  `ChordEngineProcessorIntegrationTests`, `ChordEngineFunctionalTests`.

Direct per-suite output:

| Suite | Result |
| --- | --- |
| `ChordEngineCoreTests` | all expanded checks passed (incl. the ASCII/mojibake guard) |
| `ChordEngineReferenceTests` | all 8 scales and all 12 preset maps verified from source fixtures; `hostObserved=false` |
| `ChordEngineFLReferenceVectorTests` | **38/38** source-derived vectors matched; `hostObserved=false`; `unresolved=0` |
| `ChordEngineProcessorIntegrationTests` | **17/17** native integration cases; 9/9 source-derived vector comparisons |
| `ChordEngineFunctionalTests` | **14/14** functional parity cases (new) |

No existing assertion or fixture was weakened or changed: the vector fixture is
still 38/38 with `hostObserved=false`, and the integration suite still runs the
same 17 cases.

### What the new functional suite covers

1. Fixed velocity updates Core state (1 / 64 / 100 / 127, plus rejection of
   0 and 128 without changing state).
2. Fixed velocity affects generated MIDI (1 / 64 / 100 / 127).
3. Velocity mode switching (Dynamic follows the input, Maximum = 127, Fixed
   uses the fixed value, out-of-range rejected).
4. Recorder arm state changes.
5. Recorder stop / cancel state changes.
6. INFO state model transitions (derived lines and the honest update check).
7. Trial clock transitions, including "does not run before the first chord",
   exact countdown, single expiry transition, and "a licensed session never
   consumes the trial".
8. Update state transitions against a mocked service, plus the shipped
   service's reference behaviour.
9. Sign-in transitions against a mocked auth service through the existing
   `AuthService` seam, including the proof that the boundary does not block
   waiting for the network, the one-shot approval URL, token persistence, and
   the 401 transition.
10. Account button states (SIGN IN / CANCEL LINK / SIGN OUT).
11. The licensing gate stops chord generation entirely (reference `onNoteOn`).
12. The trial usage clock starts only on a real generated chord.
13. Real bound editor callbacks update real state (recorder, navigation,
    velocity mode, fixed velocity slider, transpose bounds, update check,
    INFO version row, the expiry-gate wiring).
14. Realtime/GUI isolation: `processBlock` is source-scanned against twelve
    forbidden tokens (no URL, no licensing boundary, no auth service, no
    `MessageManager`, no editor, no repaint, no `setBounds`, no thread pool);
    the auth service must own a `juce::ThreadPool` and must marshal replies
    with `MessageManager::callAsync`; the boundary must perform no HTTP and
    open no URL itself; the processor must embed no auth endpoint.

### Artifacts

Full details and hashes are in [MANIFEST.txt](../Build/TestArtifacts/MANIFEST.txt):

- [AU Functional GUI test ZIP](../Build/TestArtifacts/ChordEngine-Native-macOS-AU-Functional-GUI-Test.zip),
  SHA-256 `9fbf819cec610ab5da5baf6a8b66e3ca39a63568e90dd9d5896f440b2b35deb5`.
- [VST3 Functional GUI test ZIP](../Build/TestArtifacts/ChordEngine-Native-macOS-VST3-Functional-GUI-Test.zip),
  SHA-256 `b67ecfb26ce98b5541f3f96f834dbf0703862a1a4a32211b4050add9543bb7cb`.

Both extract to a single root bundle, are universal (`x86_64 arm64` via
`lipo -info`), carry only an ad-hoc local signature, and keep the AU identity
unchanged (`type = aumi`, `subtype = Abcd`, `manufacturer = Abcd`,
`name = "Music-Prod: ChordEngine"`).

### Protected artifacts (re-run read-only after this build)

- Approved FL VST3 executable SHA-256 remains
  `3ffbe1f349846df64dc7e2cf3357b1e682e4f8f1dd3cf8a879080edc773483ed`.
- Protected ChordEngine-FL `Binaries/Source/Plugin.cpp` MD5 remains
  `1044c2719acb8d49573c5aadd9e7ca22`.
- No file in `HISE Projects/ChordEngine` or `HISE Projects/ChordEngine-FL` was
  modified: the newest file in the HISE ChordEngine project is from
  2026-09-24, i.e. before any session of this work.

### Pending manual verification

`Docs/SECOND_MAC_FUNCTIONAL_GUI_TEST.md` is the current clean-second-Mac
checklist. GUI rendering, control behaviour, resizing, Help launch, the
Music-Prod Studio launch, the real device-code sign-in, the live trial
countdown, the expiry gate and recorder external file drop have not been
verified in a DAW by this build. No DAW was automated. No plugin was installed
on the development Mac.
