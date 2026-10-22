# Release 0.5.0 — the first universal ChordEngine release

**Status: release candidate prepared, unsigned and unpublished.** The candidate is
built and tested locally from a clean build tree; FL Studio and Logic Pro acceptance
testing is still outstanding, and no package has been signed, notarized, installed or
published.

0.5.0 is the first **universal** release. It continues the legacy ChordEngine FL line
(0.4.0) and replaces it: one implementation, one installer, both plug-in formats, every
supported DAW, and **one shared entitlement** (product slug `chordengine`, unlocked by
either a Music-Prod+ subscription or an activated lifetime licence). Nothing has been
distributed to customers, so no field migration is involved.

## One version, one source

`CHORDENGINE_VERSION` in `CMakeLists.txt` is the only place the release version is
written. Everything derives from it:

| Surface | Value | Where it comes from |
| --- | --- | --- |
| Compiled plug-in version (`JucePlugin_VersionString`, AU/VST3 `CFBundleShortVersionString`, VST3 module `Version`) | `0.5.0` | `juce_add_plugin(... VERSION ${CHORDENGINE_VERSION})` |
| INFO page `Current version`, footer `Chord Engine v…`, updater `current=` | `0.5.0` | `chordengine::updates::currentBuildVersionString` (`JucePlugin_VersionString` of the running binary) |
| Account bridge `plugin_version` telemetry | `0.5.0` | `chordengine::licensing::productVersion` ← `CHORDENGINE_PRODUCT_VERSION` compile definition |
| Test and development binaries | `0.5.0` | one shared CMake definition list (`CHORDENGINE_PLUGIN_BUILD_DEFINITIONS`) |
| Release packaging default | `0.5.0` | `VERSION` in `Tools/PackageMacDMG.sh` (overridable with `--version`) |

Two guards keep the surfaces from drifting apart: `Source/Licensing/LicensingTypes.h`
contains a `static_assert` that refuses to compile when the telemetry version and the
compiled plug-in version disagree, and `ChordEngineReleaseMetadataTests` fails the build's
test run when the built bundles disagree with `CHORDENGINE_VERSION`.

The reference product's `0.4.0` remains only in reference documentation (it is the version
of the legacy FL artifact); no client code reports it any more.

## Frozen identities

| Format | Bundle identifier | Codes | Notes |
| --- | --- | --- | --- |
| AU (macOS) | `com.musicprod.chordengine` | type `aumi`, manufacturer `Musc`, subtype `Cefl` | First released AU identity. The AU was never distributed, so the JUCE placeholder (`Abcd`/`Abcd`, `com.myCompany.product`) was not a compatibility constraint. |
| VST3 (macOS + Windows) | `com.musicprod.chordenginefl` | manufacturer `Musc`, plug-in code `Cefl` | **Legacy-compatible**: bundle id and codes unchanged from the ChordEngine FL build. |
| pkg | `com.musicprod.chordengine.pkg` | — | Installer identifier; unchanged. |
| Licensing / updates | product slug `chordengine` | — | Shared by every format and both unlock routes. |

**Why the VST3 identity cannot move.** JUCE derives the VST3 class id from the
manufacturer code and the plug-in code: `ABCDEF01` + a fixed JUCE salt + hex(`Musc`) +
hex(`Cefl`) = `ABCDEF019182FAEB4D7573634365666C`. That pair *is* the plug-in as every
existing FL Studio project recorded it, so changing either the bundle id or a code would
orphan those projects. The AU now carries the same code pair deliberately: the two formats
are one product, and AU hosts resolve a saved project by the `(type, subtype, manufacturer)`
triple, so this was the last moment the AU codes could be chosen.

The VST3 class category stays `Instrument` (`VST3_CATEGORIES Instrument`). A `Fx` module is
not given an event/MIDI input by FL Studio, so `Instrument` is what keeps ChordEngine's MIDI
input reachable there. This still requires real FL Studio acceptance — a metadata check
cannot prove it.

## The automated release-metadata gate

`ChordEngineReleaseMetadataTests` (`Tests/ReleaseMetadataTests.cpp`, CTest name
`ChordEngineReleaseMetadataTests`) inspects the bundles **this build just produced** — never
an installed copy — and fails on a placeholder identifier, a missing field, a wrong
category, a wrong class id or a version mismatch. It checks:

* AU `Info.plist`: bundle id, version, name, exactly one `AudioComponents` entry, `aumi`,
  `Musc`, `Cefl`, `Music-Prod: ChordEngine`, present factory function and version.
* VST3 `Info.plist` and `moduleinfo.json`: bundle id, version, name, vendor, exactly one
  audio module class, the class id above, and `Sub Categories` containing `Instrument` and
  not `Fx`.
* The hosted view of the **VST3** bundle through JUCE's head-less hosting: one class, name,
  vendor, version, MIDI input, MIDI output, MIDI effect, **zero audio buses**, and silence
  on the audio path for a trigger.
* The compiled constants: plug-in version = licensing telemetry version = update-client
  version = `0.5.0`, product slug `chordengine` on both sides, and an update URL carrying
  `current=0.5.0`.
* No `Abcd`, no `com.myCompany.product`, no `0.4.0` and no "development build" text in any
  generated metadata.

It exits 77 (CTest "skipped") when the bundles have not been built, so a skipped run is
never reported as a pass. It deliberately proves nothing about host behaviour; the AU host
section is advisory because a build-tree AU is not registered with the system, and it says
so instead of pretending to pass. Real behaviour stays with
`ChordEngineHostBoundaryProbe` (MIDI in → generated chord out across the format boundary)
and with the two manual DAW sessions.

## Duplicate-install conflict: the legacy bundle and the universal VST3

The conflict is real only if a machine holds **two differently named VST3 bundles that
declare the same class id**: the legacy `ChordEngine FL.vst3` and the universal
`ChordEngine.vst3`. Because the class id is shared on purpose, a host that scans both sees
one plug-in twice — which is exactly how it is meant to look when the universal build
replaces the legacy one, and exactly the state that must not be shipped.

What actually happens per upgrade path:

| Situation | Bundles present | Result |
| --- | --- | --- |
| Fresh machine | `ChordEngine.vst3` only | one plug-in; no conflict |
| Universal 0.5.0 → later universal release | the same path is replaced | one plug-in; no conflict |
| A machine with the legacy FL build, then the universal installer | `ChordEngine FL.vst3` **and** `ChordEngine.vst3` | **conflict** — same class id, two bundles |
| A manually copied bundle in `~/Library/Audio/Plug-Ins/VST3/` plus a package install | user-domain copy **and** `/Library/…` copy | **conflict** — hosts prefer the user-domain copy, so the newer build can be ignored |

**Decision for 0.5.0: no migration or cleanup code, and no destructive installer step.**
Nothing has been distributed, so no customer machine can be in either conflict state, and
inventing a remover for installations that do not exist would be worse than the hypothetical
it guards against. The installer identifier, the payload paths
(`/Library/Audio/Plug-Ins/{Components,VST3}/ChordEngine.*`) and the "refuse to overwrite an
existing DMG/PKG unless `--force`" rule in `Tools/PackageMacDMG.sh` already describe exactly
one universal plug-in per format.

**Proposed safeguard, for review (not implemented):** if any ChordEngine build is ever
distributed besides the universal one, add a **non-destructive** `preinstall` script to the
package that

1. scans the standard plug-in locations for VST3 bundles whose
   `Contents/Resources/moduleinfo.json` declares the class id
   `ABCDEF019182FAEB4D7573634365666C`,
2. lists every bundle it finds other than the payload path, including the user-domain copy
   of the same product, and
3. stops the installation with that list and the one-line remedy, so the person installing
   decides — it must not move, rename or delete anything itself.

A release-note line ("if you previously installed a ChordEngine FL build or copied a
ChordEngine bundle into your user plug-in folder by hand, remove it after installing") is
worth adding with or without the script.

**Does 0.5.0 need a packaging change before its first public release?** No. The current
pipeline already produces a single payload per format at the correct paths, refuses to
overwrite an existing release artifact, and validates version, universal architecture,
signatures, BOM modes and notarization. The preinstall detector above becomes necessary
only for a release that is expected to land next to a legacy or hand-copied bundle.

## Backend release metadata that must be published with 0.5.0

Nothing in this repository publishes a release, and no backend file was changed for this
candidate. Until the release record below exists, the plugin honestly reports
*No published release* on the INFO page (`status: noPublishedRelease`,
`latestVersion: null`) — that is the current live answer, not a bug.

When the 0.5.0 build is actually released, the backend (`music-prod-studio-api`) needs:

1. **`release_products`** — the product row for slug `chordengine` must declare its
   `required_artifacts` slots (platform + architecture + format), because publication is
   refused while a declared artifact slot is missing. Set `download_page_url` to the
   reviewed public page (an https Music-Prod page) so an installed plugin can send the user
   somewhere official when it reports *Update available*; leave it NULL rather than
   inventing a URL.
2. **`product_releases`** — a row for product `chordengine`, `version` `0.5.0`,
   `channel` `stable`, `release_notes` describing the universal release (including the AU
   identity and the fact that there is nothing to migrate), and `minimum_supported_version`.
   `0.4.0` is the natural minimum to allow, because it is the legacy build the universal
   release replaces; anything lower should be a deliberate decision, not a default.
3. **`release_artifacts`** — one row per artifact of that release: the macOS universal
   installer (`pkg`/`dmg`, `macos`, `universal`/`x86_64+arm64`) and, if Windows is released
   in the same version, the Windows VST3 (`windows`, `x86_64`). Keep the recorded file
   digest equal to the published bytes, because the release row is immutable once published.
4. **Transition to `published`** — the release state machine moves the version that is
   currently live for the same product+channel to `yanked`, and `published_at` is frozen at
   the original publication. Publishing is out of scope here and must not be done as part of
   building the candidate.
5. **Verify from the outside** — after publication, a fresh GET to
   `functions/v1/music-prod-studio-api/updates?product=chordengine&current=0.5.0&platform=macos`
   must answer with the 0.5.0 release, and a `current=0.4.0` request must answer with the
   decision the release is meant to produce for legacy users. Both answers go through the
   plugin's own strict validator; an inconsistent payload is refused by the client.

## Local verification of this candidate

Built unsigned from a clean tree at `/tmp/ChordEngine-0.5.0-RC` with CMake 4.4.4, the Xcode
generator, `Release`, `arm64;x86_64`, pinned JUCE 9.0.3 and a 12.0 deployment floor.

* Build: succeeded, no compiler errors.
* `CTest`: **11/11 passed** (core, reference, FL reference vectors, processor integration,
  functional, update, licensing contract, licensing route switch, state, host-boundary probe,
  release-metadata gate).
* Release-metadata gate: **49/49 checks passed**, with one explicit skip - the AU cannot be
  loaded by the head-less host from the build tree, because a bundle that is not installed is
  not registered with the system. A skip is never reported as a pass.
* Generated metadata, read back from the built bundles:

| Artefact | Value |
| --- | --- |
| AU bundle id / version | `com.musicprod.chordengine`, `0.5.0`, `aumi` / `Musc` / `Cefl`, displayed as "Music-Prod: ChordEngine" |
| VST3 bundle id / version | `com.musicprod.chordenginefl`, `0.5.0` |
| VST3 class id | `ABCDEF019182FAEB4D7573634365666C` - unchanged, so the identity stays legacy-compatible |
| VST3 category | `Instrument` |

* Host-boundary probe against the freshly built **VST3** bundle: version 0.5.0, MIDI input and
  MIDI output declared, **no audio bus** (in 0 / out 0), trigger 60 generates 51/55/58/62 and
  the note-offs release every generated pitch.
* Host-boundary probe against the freshly built **AU**: not possible without installing it, so
  AU host acceptance stays with the manual Logic session. For reference, the previously
  installed AU (identical JUCE flags, `aumi`) reports one stereo output bus to the host, i.e.
  this candidate does not change the AU's host-visible bus layout.

One blocker was fixed on the way: the project's deployment-target default was `11.0`, which
the installed Xcode's macOS SDK refuses (it supports 12.0 and later), so the C and C++
compiler identification failed before any project code compiled. The floor is `12.0` now,
which is what the product and its host-test checklists already state.

## Remaining acceptance work (manual, by a human, no automation)

**FL Studio (VST3)** — the release-critical one, because it is the only evidence that
`Instrument` + no audio bus gives FL an event/MIDI input and a workable channel role:

1. Install/rescan and confirm ChordEngine appears **once**, as Music-Prod → ChordEngine, in
   the role FL assigns an `Instrument` class with no audio bus (and confirm that it can be
   loaded on a channel or in Patcher and still receive events).
2. Trigger 60 with the factory defaults → the generated chord is **51, 55, 58, 62**, and the
   raw trigger note is not passed through as a played note.
3. Route the generated MIDI to a **fresh, normal instrument** at a safe output level and
   confirm correct pitches.
4. Confirm ChordEngine itself produces no audio, and that note-off, CC64 sustain and
   CC120/CC123 panic behave.
5. Save a project, reopen it, and confirm the plug-in and its settings are restored — plus
   that ChordEngine is still listed once after a rescan.

**Logic Pro (AU)** — the AU is new to the field and its composed identity is untested:
`Docs/LOGIC_HOST_TEST_CHECKLIST.md` sections A–G (discovery, MIDI FX insertion, musical
controls, piano/MIDI behaviour, recorder, state persistence E1–E7, licensing/INFO rows,
stability, and G3 "listed once, not duplicated"), on a new disposable project only.

Neither session may be automated, and neither can be replaced by the automated suites.

## Final candidate status (2026-10-17)

**Status: final source candidate built and tested locally. Unsigned, unpublished, and
not yet accepted in FL Studio or Logic Pro.**

It contains the editorial V2 redesign, the script wordmark and the INTERNAL AUDIO
wrap fix. Build: fresh Xcode Release tree in `/tmp` (throwaway), `arm64` + `x86_64`,
`minos 12.0`, JUCE 9.0.3.

* **CTest:** 11/11 passed.
  * Functional: 22/22.
  * Release-metadata gate: 49/49, with the AU host check skipped.
* **Host-boundary probe:** its CTest run has no arguments, so it loads the bundles
  *installed in `~/Library` on the build Mac*, not the fresh build. A CTest pass
  therefore does not cover the candidate.
  * Run against the freshly built VST3 by path: 0.5.0, no audio bus, and trigger 60
    gives 51/55/58/62.
  * The build-tree AU cannot be loaded unregistered, so that check is not done.
* **Metadata:**
  * Identity: AU `aumi`/`Musc`/`Cefl`, `com.musicprod.chordengine`. VST3
    `com.musicprod.chordenginefl`, class id `ABCDEF019182FAEB4D7573634365666C`,
    `Instrument`.
  * Version 0.5.0 everywhere.
  * Signatures are ad-hoc only.
* **DAW acceptance:** blocked. The test Mac was in active local use (continuous HID
  input, RustDesk connected, FL Studio just launched). Freebuff also still lacks
  Screen Recording and event-posting permission there.
  * The test Mac's installed 0.5.0 bundles are the earlier, pre-redesign candidate.
    They match the 2026-10-17 installation ledger and were left untouched.
* **Signing and notarization:** not started. Waiting on host acceptance.

## Release candidate 20261017c (2026-10-17) - supersedes the status above

**Status: release candidate built, tested headless, Developer ID signed, notarized
and stapled. Not published. FL Studio and Logic Pro acceptance still outstanding.**

### Bundle-signing defect fixed in the build

The earlier candidate's bundles both failed `codesign --verify --deep --strict`:

* **AU**: Xcode signing is disabled for this project and JUCE only ad hoc signs
  plug-in bundles from its copy step, which is off. The AU therefore carried only the
  linker's `-adhoc_codesign` signature: Info.plist not bound, no sealed resources
  ("code has no resources but signature indicates they must be present").
* **VST3**: JUCE re-signs the VST3 *before* its manifest step writes
  `Contents/Resources/moduleinfo.json`, so that file was unsealed ("a sealed resource
  is missing or invalid").

Fix: `chordengine_adhoc_sign_bundle` in `CMakeLists.txt` adds a final POST_BUILD
command to `ChordEngineAU_AU` and `ChordEngineVST3_VST3` that runs
`Tools/AdhocSignBundle.cmake`: `codesign --force --sign - --timestamp=none <bundle>`,
then fails the build unless strict verification passes. It is added after
`juce_add_plugin`, so it runs after the manifest step; `--force` makes it safe on
incremental builds. Option `CHORDENGINE_ADHOC_SIGN_BUNDLES` (default ON, Apple only).

**Signing order.** Build: link (linker ad hoc) -> JUCE POST_BUILD steps (VST3:
JUCE re-sign, `moduleinfo.json`) -> **ChordEngine ad hoc bundle signature + strict
verify**. Release: `Tools/PackageMacDMG.sh` copies the built bundles into a staging
root and re-signs the copies with the Developer ID Application identity (hardened
runtime, secure timestamp); the build tree keeps its ad hoc signature. Ad hoc and
Developer ID signing never touch the same files. The bundles contain one universal
Mach-O and no nested code, so signing the bundle is the whole order.

Test changes:

* `ChordEngineReleaseMetadataTests` now also requires strict signature verification,
  a bound Info.plist and sealed resources for both bundles (55 checks). Run against the
  old candidate's bundles it fails 4 checks, i.e. it catches this defect.
* CTest now passes explicit bundle paths of THIS build to
  `ChordEngineReleaseMetadataTests` (AU + VST3) and `ChordEngineHostBoundaryProbe`
  (VST3). Previously the probe ran without arguments and tested the bundles installed
  in `~/Library`. The build-tree AU is not probed: JUCE resolves AUs through the
  system AudioComponent registry, which only knows installed components.

### Build procedure used

From the repository root (throwaway build tree in `/tmp`):

```sh
O=/tmp/ChordEngine-0.5.0-RC-final-20261017c
cmake -S . -B "$O/build" -G Xcode -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$O/build" --config Release
ctest --test-dir "$O/build" -C Release --output-on-failure
"$O/build/Release/ChordEngineHostBoundaryProbe" \
  "$O/build/ChordEngineVST3_artefacts/Release/VST3/ChordEngine.vst3"
CHORDENGINE_BUILD_DIR="$O/build" CHORDENGINE_DIST_DIR="$O/dist" \
  ./Tools/PackageMacDMG.sh --version 0.5.0
```

### Results

* Configure, build: exit 0. Both bundles `x86_64 arm64`, `minos 12.0`,
  `LSMinimumSystemVersion 12.0`, version 0.5.0.
* Identities unchanged: AU `aumi`/`Cefl`/`Musc`, `com.musicprod.chordengine`,
  factory `ChordEngineAUFactory`; VST3 `com.musicprod.chordenginefl`, class id
  `ABCDEF019182FAEB4D7573634365666C`, `Instrument`, `GetPluginFactory` exported.
* Strict signature verification: AU and VST3 pass (VST3 seals `moduleinfo.json`).
* CTest 11/11: core, reference, FL reference vectors 38/38, processor integration
  17/17, functional 22/22, update 16/16, licensing contract 10/10, licensing route
  switch 10/10, state, host-boundary probe (fresh VST3), release-metadata gate 55/55
  (one documented skip: the unregistered AU cannot be hosted).
* Host-boundary probe, fresh VST3 by path, and again on the Developer ID signed
  payload VST3: trigger 60 -> 51/55/58/62, all released, two consistent passes, no
  audio bus. Exit 0. Not a DAW test.
* GUI snapshots at 640x520, 800x650, 1280x1040 (chord, info, activation, recorder):
  no regression.
* Packaging: both bundles Developer ID signed with hardened runtime; pkg productsigned;
  pkg and DMG notarized **Accepted** (submissions `004d5151-d8d8-47af-aab7-3a4cac1dde9c`,
  `c2dbf15a-7b18-472c-8016-2f3652aa7443`), stapled, Gatekeeper "Notarized Developer
  ID".

### Artifacts (SHA-256)

| Artifact | SHA-256 |
| --- | --- |
| `dist/ChordEngine-0.5.0-macOS.dmg` | `3eca3fc13cf4af07979907e941b8bce21c56a8a60e92f9505a1135dfde0996af` |
| `dist/ChordEngine-0.5.0-macOS.pkg` | `caf084dc08c0a3a607268c30057d97eecd71b3a36f548f0e9307557e13b51586` |
| payload AU binary (Developer ID) | `6cafd82ca17daa41fa2d7e1dd866197d0b8e027e5bfc7b0dd3e7ce626d14f876` |
| payload VST3 binary (Developer ID) | `816798e8c85fd46a9f88734546730413bfaf3f49a0da48364ecaae91c29ccff7` |
| build AU binary (ad hoc) | `03cddbff02d150f2052da0240ae7ab833abeb6eb8c52a144ff4612b806e79ddd` |
| build VST3 binary (ad hoc) | `d8f68d421e40b912b54658fa7ae00f6f06820d3ba08b2c3786b11d88c9651051` |
| `moduleinfo.json` | `d32e813741dd07b9f66c1e3211f7183f21a94fdf501dee4e46383bae9c403672` |

Per-file manifests are in `$O/manifests/` (build and payload).

### Host acceptance

Not performed for this candidate. Read-only checks of the test Mac (23:24, 23:29,
23:31, 23:32) showed continuous local input from its physical mouse (HID idle
0.1-0.2 s) and FL Studio at 75-106 % CPU, so the desktop was not taken over.

Prepared without touching the desktop or any installed plug-in (test Mac,
`Martins-MacBook-Pro`, macOS 26.7):

* Staged, not installed: `~/ChordEngine-0.5.0-RC-20261017c-staging/` holds the pkg,
  the DMG and the payload manifests. Hashes re-checked on the test Mac (equal to the
  table above), pkg signature "signed by a developer certificate issued by Apple for
  distribution", stapled ticket valid.
* Verified backup of the installed bundles:
  `~/ChordEngine-plugin-backup-20261017-233340/`. Installed state at backup time: only
  user-domain copies, `~/Library/Audio/Plug-Ins/Components/ChordEngine.component` (tree
  hash `79a9eee0…5e80`) and `~/Library/Audio/Plug-Ins/VST3/ChordEngine.vst3` (tree hash
  `d30088c6…dbdb`), both ad hoc 0.5.0 dated 16 Oct 20:50; nothing in
  `/Library/Audio/Plug-Ins`. Copy and source compared equal; per-file manifests are in
  the backup folder.

Two facts that shape the install:

* `sudo` on the test Mac needs a password, so the pkg (which installs to `/Library`)
  can only be installed by the owner at the machine, or by an agent session the owner
  authorises there.
* The user-domain copies shadow a `/Library` install, so they must be moved into the
  backup folder (not deleted) before testing, or the hosts will load the old build.

### Installing the candidate on the test Mac (when it is free)

Use the notarized pkg, which installs to `/Library/Audio/Plug-Ins/{Components,VST3}`.
The test Mac also holds older user-domain copies in
`~/Library/Audio/Plug-Ins/{Components,VST3}/ChordEngine.*` (8 Oct); hosts prefer the
user-domain copy, so those must be moved aside or the old build is what gets tested.

1. Verify the transferred pkg: `shasum -a 256 ChordEngine-0.5.0-macOS.pkg` must equal
   the table above.
2. Back up, do not delete: `B=~/ChordEngine-plugin-backup-$(date +%Y%m%d-%H%M%S)`;
   `mkdir -p "$B/Components" "$B/VST3"`; `ditto` each existing
   `~/Library/Audio/Plug-Ins/{Components,VST3}/ChordEngine.*` and any
   `/Library/Audio/Plug-Ins/{Components,VST3}/ChordEngine.*` into `$B`, record
   `shasum` manifests of source and copy, and compare them.
3. Only after the backup compares equal: move the user-domain copies into `$B/moved`
   (a move, not a delete) and run `sudo installer -pkg ChordEngine-0.5.0-macOS.pkg
   -target /`.
4. `killall -9 AudioComponentRegistrar` (re-scan), then
   `auval -v aumi Cefl Musc`.
5. FL Studio: rescan, load ChordEngine, route its MIDI out to 3x Osc, trigger C4 ->
   expect D#3 G3 A#3 D4, check release, chord changes and repeated playback.
   Logic Pro: new project, MIDI FX slot on a software-instrument track.

**Recovery:** remove `/Library/Audio/Plug-Ins/{Components,VST3}/ChordEngine.*`
(`sudo rm -rf`), `ditto` the backed-up bundles from `$B` back to their original
paths, `killall -9 AudioComponentRegistrar`, rescan in the host.

### Test Mac install and auval (2026-10-18)

Done over SSH after the owner approved remote work. FL Studio was closed, and Logic
Pro was open but had no ChordEngine file mapped (`lsof`).

1. Backup re-verified: both bundles in
   `~/ChordEngine-plugin-backup-20261017-233340/` still match their manifests. The
   installed user-domain binaries matched them too (AU `2f6cc2d6…7440`, VST3
   `50468f8c…1384`).
2. The notarized pkg was expanded without sudo (`pkgutil --expand-full`) into
   `~/ChordEngine-0.5.0-RC-20261017c-staging/expanded/`. The payload is equal to both
   payload manifests. `codesign --verify --deep --strict` passes. Authority is
   Developer ID Application (3A4R5EKM7V) with hardened runtime, and `spctl` reports
   "Notarized Developer ID".
3. The previous user-domain bundles were moved (not deleted) to
   `~/ChordEngine-plugin-replaced-20261018-095438/{Components,VST3}/`. The Developer ID
   payload bundles were `ditto`'d to `~/Library/Audio/Plug-Ins/{Components,VST3}/`.
   Installed tree hashes equal the payload manifests, strict verification passes, and
   there is no quarantine attribute. Nothing was installed to `/Library`, because the
   pkg needs sudo with a password. The user-domain copies are byte-identical to the pkg
   payload.
4. `auval` must run in the logged-in (Aqua) session. From an SSH (Background) session
   the AU registry lists only Apple's 56 components, not even the third-party AUs in
   `/Library`, so it fails with "didn't find the component". Run through
   `open -a Terminal run-auval.command` in the Aqua session, the result was
   `auval -v aumi Cefl Musc`: **AU VALIDATION SUCCEEDED**, Component Version 0.5.0
   (0x500), exit 0. The log is `auval-aqua-20261018.log` in the staging folder
   (sha256 `fa08727d…71f4b`).

In-host FL Studio and Logic Pro acceptance has **not** been performed. Neither the
SSH session nor Terminal in the Aqua session holds Accessibility, Screen Recording or
event-posting permission (all three preflights are false), so the DAWs can't be driven
remotely. A passing `auval` is not Logic Pro acceptance.

**Recovery for this install:** move
`~/Library/Audio/Plug-Ins/{Components,VST3}/ChordEngine.*` aside, `ditto` the bundles
from `~/ChordEngine-plugin-replaced-20261018-095438/` (or the 20261017 backup) back to
the same paths, run `killall -9 AudioComponentRegistrar`, and rescan in the host.

### Remaining before public distribution

* FL Studio VST3 acceptance and Logic Pro AU in-host acceptance. `auval` passed on
  2026-10-18, and the candidate is installed in the user domain on the test Mac.
* Backend release records (see above) and publication - separately authorised.

## Owner feature requests (2026-10-18)

This section supersedes the host-acceptance status above: the plug-in changed
after that candidate, so FL Studio, Logic Pro and Ableton acceptance must be done
on the new candidate.

### 1 and 5. Dragging a take into Logic Pro / FL Studio dropped nothing

**Cause:** `startRecorderTakeDrag` wrote the take to a temp file and deleted it
as soon as the drag session ended (`callAsync` from the drag-end callback).
Logic Pro and FL Studio read a dropped file after the session ends, so the file
was already gone. The temp name was also `temp_XXXX.ChordEngine_Take_N.mid`.

**Fix:**
- Takes are written to
  `~/Library/Application Support/Music-Prod/ChordEngine/Takes/ChordEngine Take N YYYYMMDD-HHMMSS.mid`.
- The file is kept after the drop.
- Takes older than seven days are pruned when the next take is dragged.

**Related bug fixed:** retriggering a held note turned the earlier note-on into
a note-off, which erased that note from the take. A note-off is now inserted
before the new note-on (integration case 19).

**Recording without RECORD ARM:**
- With **FOLLOW DAW RECORD** on (a switch under RECORD ARM, off by default,
  saved with the project), the recorder follows the host transport. When the
  DAW starts recording, an idle recorder arms itself. When the DAW stops, the
  take is stored, ready to drag (`followHostRecording`, integration case 18). A
  take armed by hand is left alone. See "FOLLOW DAW RECORD (2026-10-21)".
- To record the generated notes **directly onto the Logic Pro track** while
  Logic records, use Logic's own feature: the MIDI FX slot's menu, then
  "Record MIDI to Track Here". The RECORD ARM tooltip says so when the host is
  Logic.
- Logic records the input of a MIDI FX, not its output, unless that option is
  chosen. No plug-in can change that.

### 2. Preset Pack store

See `Docs/PRESET_PACKS.md`. In short:
- The stock pack is built in.
- The test pack ships as `Resources/Packs/chordengine-test-pack.json`.
- Downloads require an active Music-Prod+ membership, enforced by the server.
  The client hides pack presets from non-members.
- The backend code is in `Backend/` and is **not deployed**.

### 3. Fonts

- Interface text uses the music-prod.com typefaces: Inter for text and Schibsted
  Grotesk for captions. Both are SIL OFL 1.1.
- The files are static instances cut from the variable fonts VYRE ships, stored
  in `Resources/Fonts/` with their licences, and embedded as binary data. They
  are loaded privately.
- The chord name and INFO title keep the classic serif, and the "Chord Engine"
  wordmark keeps its script face.
- Font files: `Inter-Regular.ttf`, `Inter-SemiBold.ttf`,
  `SchibstedGrotesk-SemiBold.ttf`.

### 4. Internal audio

- New module: `Source/Audio/InternalSynth` (Synth, Piano or Bass). Bass plays
  the lowest chord note.
- 16 voices, real-time safe, and the output is limited below -1 dBFS.
- **VST3:** now has one stereo output (no input). Internal audio is off by
  default, so routing the MIDI out to another instrument (FL Studio 3x Osc)
  behaves exactly as before. The class id and category are unchanged.
- **Logic Pro:** a MIDI FX slot carries no audio, so a **second AU** was added
  on the owner's decision: "ChordEngine Instrument" (`aumu` / `Cein` / `Musc`,
  `com.musicprod.chordengine.instrument`). Internal audio is on by default. The
  MIDI FX AU (`aumi` / `Cefl` / `Musc`) is unchanged and shows a chip pointing to
  the instrument.
- The internal-audio switch and sound are saved in the host state
  (`internalAudio`, `internalSound`). Payloads without them keep the build's
  default.
- The release gate (`ReleaseMetadataTests`) now also checks the instrument AU's
  identity and signature, and the VST3's single stereo output, which must stay
  silent while internal audio is off.
- `Tools/PackageMacDMG.sh` installs all three bundles.

### 6. Ableton Live

Not tested: neither Mac has Ableton Live installed. What to expect:
- Live does not load AU MIDI processors (`aumi`).
- Live loads the **VST3**, or the **ChordEngine Instrument** AU, as an
  instrument.
- With internal audio on, it sounds on its own track.
- To drive another instrument, create a MIDI track whose "MIDI From" is the
  ChordEngine track, set to "ChordEngine" (post FX).
- Dragging a take into Live's arrangement uses the same persistent file as Logic
  and FL.
- A human acceptance run is still required.

### Release candidate 20261018a (owner feature requests)

**Build:**
- Universal (arm64 + x86_64) build in `/tmp/ChordEngine-0.5.0-RC-20261018a`,
  using the procedure above.
- CTest: 13/13. That covers Pack 9/9, InternalSynth 121 checks, integration
  21/21, functional 23/23, the release gate with all three bundles, and the
  host-boundary probe.
- All three bundles pass `codesign --verify --deep --strict`.

**Packaging:**
- `Tools/PackageMacDMG.sh` signed all three bundles with Developer ID and the
  hardened runtime.
- Notarization was Accepted for the pkg (`f9f26dc3-1864-42ce-84da-c41da9af6baa`)
  and the DMG (`0bd5101c-6a24-4124-8bc8-e817b929d2ba`). Both are stapled, and
  Gatekeeper reports "Notarized Developer ID".

| Artifact | SHA-256 |
| --- | --- |
| `ChordEngine-0.5.0-macOS.pkg` | `2071eae29d436f27d0995a8ceee5905203dbdfa63e1b353ceb7df22ed6968c3f` |
| `ChordEngine-0.5.0-macOS.dmg` | `7096eaff9d48a619108094c7faa8812acae8bb1782cfefbae1068c5d791f9d78` |

**Test Mac (2026-10-18 10:52):**
- The hashes were re-checked on arrival.
- The Developer ID payload of all three bundles was installed to
  `~/Library/Audio/Plug-Ins`.
- The previous 20261017c user-domain bundles were moved (not deleted) to
  `~/ChordEngine-plugin-replaced-20261018-105209/`.
- No open DAW had a ChordEngine file mapped at the time.
- `auval`, run in the Aqua session:
  - `aumi Cefl Musc`: AU VALIDATION SUCCEEDED (0.5.0)
  - `aumu Cein Musc`: AU VALIDATION SUCCEEDED (0.5.0)
  - The log is `~/ChordEngine-0.5.0-RC-20261018a-staging/auval-aqua.log`.

**Still required (by a person, in the hosts):**
- **FL Studio (VST3):**
  - Drag a take into the playlist.
  - Internal audio on and off, with each sound.
  - MIDI out to 3x Osc still works.
- **Logic Pro:**
  - MIDI FX drag-out.
  - Record with the transport, then "Record MIDI to Track Here".
  - The ChordEngine Instrument sounds.
- **Ableton Live:** not installed on either Mac.
- **Store:** cannot be accepted until `Backend/` is deployed.

### Owner retest of 20261018a (11:06) and fixes in 20261018b

**Logic Pro:** dragging a take out now works (owner).

**FL Studio:** dragging a take into the playlist still dropped nothing (owner's
screen recording). Evidence from the test Mac:
- The take files were written. `Takes/` holds five files from 11:06 to 11:09.
- The unified log shows FL's drop target reading the drag pasteboard
  (`public.file-url` and `NSFilenamesPboardType`) at 11:06:23-24 and 11:06:25-26.
  The session then ended without a drop.

So the file reached FL, and FL declined the drop.
- JUCE's `performExternalDragDropOfFiles` offers a target only
  `NSDragOperationCopy`.
- Finder offers copy, generic, link and move for the same file.

**Fix:** `Source/UI/ExternalFileDrag.mm` is ChordEngine's own `NSDraggingSource`.
- It offers copy, generic, link and move (never delete).
- It is registered under a per-binary unique Objective-C class name.
- It sends the mouse-up the view misses, as JUCE does.
- It logs the operation the target performed to `Takes/drag-log.txt`
  (`operation=0` means refused). One more drag in FL settles the question.
- **Fallback:** right-clicking a take offers "Show in Finder", from where the
  file can be dragged in.

**Clipping:** the synth's tanh shoulder (unity to 0.6, approaching 0.89)
saturated dense chords. It was replaced by:
- a fixed headroom gain of 0.6 (about -4.4 dB);
- a zero-latency peak limiter with a ceiling of -3 dBFS and a 150 ms release,
  plus a final clamp at the ceiling.

A 6-note chord at velocity 127 now peaks at about -10 dBFS (synth), -11 dBFS
(piano) or -6 dBFS (bass). Every voice-stealing overload stays at or below
-3 dBFS. The synth tests now assert the -3 dBFS ceiling.

**Candidate 20261018b:**
- Universal build in `/tmp/ChordEngine-0.5.0-RC-20261018b`. CTest 13/13.
- Notarization Accepted for the pkg (`fb5f5ed4-70a0-4520-9296-a69af2fc9aac`) and
  the DMG (`53405a9b-9e5e-4915-bc6b-a48d50998c0f`).
- pkg `308a159f854da8a36ab3ca6783a411d00a979f5beb01ebfab1e3f29715619b32`
- DMG `7018099d2013b6c7977d26b3c40d41abd433d4995be96575c1ae30c889a535c3`

**Test Mac (11:28):**
- Installed to `~/Library/Audio/Plug-Ins`. The previous bundles were moved to
  `~/ChordEngine-plugin-replaced-20261018-112846/`.
- No DAW was running.
- `auval` passed for both AUs (Aqua session).
- The FL Studio drag retest is still to be done.

## Owner retest and recorder quantize (2026-10-20)

### FL Studio drag-out result

- **The Playlist is not a drop target for MIDI files on the test Mac.** Dragging a take
  onto the Playlist was refused (`Takes/drag-log.txt`: `operation=0`).
- **This is not specific to ChordEngine.** The same take dragged from Finder failed too,
  and so did a format-0 copy, a two-track format-1 copy and a MIDI file shipped with
  FL Studio. The unified log shows FL reading the file URL and never accepting the drop.
- **The supported FL workflow** is to drag the take onto a channel in the Channel Rack
  (confirmed working by the owner), then place that pattern in the Playlist. A Piano
  Roll drop or File > Import > MIDI file also works.
- **No code change for drag-out.**

### Recorder quantize

**Owner report:** a dragged-out take was not in sync with the host grid. The recorder
timestamps notes from the first played note (tick 0) using elapsed samples and the
host tempo, so played timing and the first note's position in its bar both carried
over.

**Change:** a selector in the recorder title row, with FL Studio's list.
- The options are No quantize, 1/6, 1/4, 1/3 and 1/2 step, Step, 1/6, 1/4, 1/3 and
  1/2 beat, Beat, and Bar. A step is a 1/16 note.
- **Export-only:** stored takes keep the played timing. The grid applies whenever a take
  is exported, so another grid can be picked and the take dragged again.
- **Note starts snap to the nearest grid line, and note lengths are kept**, as FL
  Studio's quick quantize does. When the host transport is running at the first note,
  its bar position (`ppqPosition`, last bar start, time signature) anchors the grid.
  The exported file then starts at that bar line, so dropping it on that bar lines up
  with the host. With the transport stopped, the grid is anchored to the first note.
- **Repeated pitches never overlap:** an earlier note is cut at the next start of the
  same pitch, and dropped if both land on one line.
- **The take keeps the host's time signature**, written as the file's time-signature
  event instead of a fixed 4/4.
- **The grid is saved in host state** (`recordQuantize`, appended token list). Older
  payloads keep the default, No quantize, which exports exactly as before.
- **Code:** `Source/MIDI/TakeQuantize.h` (pure, header-only) and
  `ChordEngineAudioProcessor::{set,}recordQuantize` and `captureRecorderGridAnchor`.
  The export now copies the take under its lock and quantizes and writes without it.
- **Tests:** integration case 22 covers every grid spacing, none unchanged, lengths kept,
  host-anchored placement inside the bar, Bar and early-chord handling, repeated pitches,
  the end-to-end export with a running transport, and the state round trip.
  - CTest 13/13 on a fresh universal build; integration 22/22.
  - Snapshot `-recorder` now also renders at 640x520 and 1280x1040, with a grid selected.

**Not yet verified in a host:** an FL Studio or Logic Pro check is still needed that a
quantized take lands on the grid. No candidate has been signed or installed with this
change.

### Candidate 20261020a (recorder quantize), ad hoc test build

**Build:**
- Fresh universal build in `/tmp/ChordEngine-0.5.0-RC-20261020a`, using the procedure
  above: configure, build, `ctest` and the fresh-VST3 probe all exit 0.
- CTest 13/13. The probe gives 51/55/58/62 on this build's VST3.

**Bundles:** all three are `x86_64 arm64`, version 0.5.0, and pass
`codesign --verify --deep --strict`. They carry an ad hoc signature only.
- Tar `ad0a9bc85a2f292e494487421dce93c9a486e181a8de3495d26cb3ce35084afc`.
- Per-bundle manifests are in `$O/manifests/`.

**Signed and notarized:** a first attempt was blocked by the agent session's
permission policy. After the owner granted `Tools/PackageMacDMG.sh` (project-local
allow rule in `.claude/settings.local.json`), packaging succeeded.
- All three bundles are signed with Developer ID Application (3A4R5EKM7V) and the
  hardened runtime.
- pkg notarization **Accepted** (`10678251-6e93-452e-9647-554f5ccff95d`); DMG
  notarization **Accepted** (`be2110f0-698b-41ee-997c-5cdda791908e`).
- Both are stapled, and Gatekeeper reports "Notarized Developer ID".
- `$O/dist/ChordEngine-0.5.0-macOS.pkg`
  `0f87fd2aceb61597b90a7701355090c49cead68d55c1e0eaed5d2ac402cd7024`
- `$O/dist/ChordEngine-0.5.0-macOS.dmg`
  `3bda1ec5a12105885c6d3a66f4ea7e8d40d433535a998b70b860b92d60fd9441`
- Signed-payload tar `4e851b1f8297ac1adc0cb1c96e85997e525d26ac5377e8003717923ff991919e`.
  Its manifests are in `$O/manifests-signed/`.

**Test Mac:**
- Staged in `~/ChordEngine-0.5.0-RC-20261020a-staging/`.
  - `bundles/` holds the Developer ID payload extracted from the notarized pkg. Its
    manifests, strict verification, the pkg signature and the stapled ticket were
    re-checked on the test Mac.
  - The earlier ad hoc build is kept in `bundles-adhoc/`.
- `swap-in.sh` there moves the installed 20261018b bundles to
  `~/ChordEngine-plugin-replaced-<ts>/` and installs the candidate. It refuses while
  any app has a ChordEngine binary open; FL Studio had the VST3 loaded at staging time.
- **Recovery:** move the new bundles aside, then move the `replaced` bundles back.

Installed on the test Mac on 2026-10-21 with `swap-in.sh` after FL Studio was closed: all
three user-domain bundles match the signed manifests, pass `codesign --verify --deep
--strict` and carry `Developer ID Application: Martin Kadziolka (3A4R5EKM7V)`. The
20261018b bundles were moved to `~/ChordEngine-plugin-replaced-20261021-125608/`. The
admin panel's Plugin Releases page still lists ChordEngine with no stable/beta/development
release, which is expected: no `product_releases` row exists for it yet (see "Backend
release metadata" above).

## FOLLOW DAW RECORD (2026-10-21)

Owner request after the quantize retest: the automatic arm on the host's record
button must be a user choice, off by default.

- New switch **FOLLOW DAW RECORD: ON/OFF** directly under RECORD ARM
  (`follow-host-record`). Saved in the host state as `followHostRecord="on|off"`;
  older payloads without it keep the default, off.
- On: pressing Record in the DAW arms an idle recorder (the take starts at the first
  generated note), and stopping stores the take. Off: only RECORD ARM records.
- Host recording now counts only while the transport is playing as well, because
  FL Studio can report its record switch while stopped.
- Switching it off while a host-armed take is open hands that take to the user (it is
  not stopped by the DAW any more); switching it on during a DAW recording waits for
  the next record start.
- The empty-recorder hint and the RECORD ARM tooltip mention the DAW only when it applies.
- Integration case 18 covers: default off, no arm while off, mid-recording switch-on,
  stopped-transport record switch, switch-off hand-over, and the state round trip.
  CTest 13/13 passed in `Build/macOS-JUCE9` (reconfigured with the documented command,
  because that build tree predated the newer test targets).

## Candidate 20261021a (FOLLOW DAW RECORD) and the distribution path (2026-10-21)

Built from the working tree with the documented configure in
`/tmp/ChordEngine-0.5.0-RC-20261021a/build`; CTest 13/13; `PackageMacDMG.sh --version 0.5.0`
exit 0. Both notarizations Accepted, stapled, Gatekeeper "Notarized Developer ID". **Not
installed on the test Mac** (owner wants to receive it through Music-Prod Studio).

| File | Bytes | SHA-256 |
| --- | --- | --- |
| `ChordEngine-0.5.0-macOS.pkg` (inside the DMG) | 26711219 | `556b9dae5be198cf52a37ff4cca4d83c6b0bfe51fd37c3933ef8ec7bb9c4d3ad` |
| `ChordEngine-0.5.0-macOS.dmg` | 27807138 | `6a92017cfc6252023b1cca54aa7e59bf2d7da33975348e3dc7a08c3004f515b7` |

The pkg installs the three bundles into `/Library/Audio/Plug-Ins/{Components,VST3}`.

What the release system needs (read-only survey of the website and Studio repos, 2026-10-21):

- **Website download (`/chordengine`, `src/pages/ChordEngineLanding.tsx`)** is already
  dynamic: it asks `music-prod-studio-api/updates?product=chordengine&platform=macos` for
  the published release and offers its `dmg` artifact through `/downloads/authorize`. It
  shows "Mac download coming soon" until a ChordEngine release with a macOS DMG is
  **published**. The download is gated to signed-in Music-Prod+ members or licence holders.
- **Release record**: admin Plugin Releases page: create `chordengine` 0.5.0 stable (build
  number), upload the DMG in the "macOS universal DMG" slot with the inner pkg name and
  SHA-256 above, then validate, submit, approve, publish. The server stores the file and
  computes its own SHA-256; it must equal the table.
- **Music-Prod Studio** already lists `chordengine` and reads the installed version from
  `ChordEngine.component` / `ChordEngine.vst3`, but plugin installation is not implemented for
  any plugin yet (download + checksum only, DMG not accepted, install gate off). That is work
  in the Music-Prod Studio repository, not here.
- **Version for an update test**: an update is offered only when the published version is
  higher than the installed one. The test Mac holds 0.5.0 candidate bundles in
  `~/Library/Audio/Plug-Ins` (user domain), so a published 0.5.0 is "up to date" there; the pkg
  also installs to `/Library`, which would leave two copies. Either publish this build as 0.5.0
  (website) and test the app update with the next version, or remove the user-domain copies on the
  test Mac before the app test.

### Release record submitted for approval (2026-10-21)

Created through the release publisher API (publisher credential, write scope); **not approved,
not published** — the owner approves and publishes in Admin → Plugin Releases.

- Product `chordengine` (`dc87e850-f032-4acf-880f-d9330e415c30`), `access_model = licensed`
  (unchanged), `required_artifacts = []`, `download_page_url = null`.
- Release `11d91ba3-be5d-49a8-a541-3bdb75fb2d09`: 0.5.0, stable, build 1, not mandatory,
  minimum macOS 12.0, no minimum supported version. Status `ready_for_approval`.
- One artifact `f6104e2f-a117-4123-aa30-42f8cfbae0b7`: `dmg`, macos, universal,
  `ChordEngine-0.5.0-macOS.dmg`, 27807138 bytes; server-computed SHA-256 equals
  `6a92017c…d3ad` above.
- `installerMetadata`: container `dmg`; pkg `ChordEngine-0.5.0-macOS.pkg`, sha256
  `556b9dae…d3ad`, 26711219 bytes, identifier `com.musicprod.chordengine.pkg`, version
  0.5.0, install location `/`; codesign Developer ID Installer: Martin Kadziolka
  (3A4R5EKM7V), signed 2026-10-21T12:41:51Z; notarization accepted, stapled, submission
  `29dc47b7-1217-4403-aaae-0aeabda0de3b` (the PKG; the DMG's own submission is
  `9e38f896-0b30-4808-9b86-f020f03ac2f7`); minimum macOS 12.0. The PKG inside the DMG was
  hashed from the mounted image and equals the standalone PKG.
- Public `GET /updates?product=chordengine` still answers "no published release exists".
