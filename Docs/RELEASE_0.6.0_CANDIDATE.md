# ChordEngine 0.6.0: signed release candidate (not released)

Built 2026-10-22 from branch `juce-free` at `fb1e78f` (the JUCE-free rebuild,
`Docs/NATIVE_REBUILD.md`). Signed and notarized; **not installed, not
published, no release record**. The owner approves the release separately.

## Artifacts

Local folder (git-ignored): `Build/ReleaseArtifacts/ChordEngine-0.6.0/`.

| File | Bytes | SHA-256 |
|---|---|---|
| `ChordEngine-0.6.0-macOS.dmg` | 3760636 | `2bae51dd5709e1c122045197c690fd497b1a41945365246b0a4b2b188438953a` |
| `ChordEngine-0.6.0-macOS.pkg` (also inside the DMG) | 3733064 | `a30a33094141df9a9b896b0cecbae6affcf380d5d63a27c512e941d46a9de66e` |

## How it was built

```sh
cmake --build Build/native -j 10 && ctest --test-dir Build/native   # 20/20
NOTARY_KEY=~/.private_keys/AuthKey_<key id>.p8 NOTARY_KEY_ID=<key id> \
NOTARY_ISSUER_FILE=~/.pumpcurve/asc-issuer.txt \
CHORDENGINE_DIST_DIR="$PWD/Build/ReleaseArtifacts/ChordEngine-0.6.0" \
  Tools/PackageMacDMG.sh --version 0.6.0
```

Same identities and notary credentials as VYRE 1.0.0: Developer ID
Application / Developer ID Installer: Martin Kadziolka (3A4R5EKM7V); App Store
Connect API key with the issuer ID from `~/.pumpcurve/asc-issuer.txt` (values
never printed). Notary submissions: pkg `78ef3d7e-618f-441a-b666-df40d5b385e6`
Accepted, DMG `5b8e0275-fd37-4e12-a51d-c2ac5693e4f1` Accepted; both stapled.

## Release checks (run on the bundles extracted from the signed pkg)

| Check | Result |
|---|---|
| `Tools/CheckBundles.sh` (ids, versions 0.6.0, AU version 1536, PkgInfo, universal, macOS 12.0, exports, notices, strict signature) | 0 failures |
| `Tools/CheckNoJuce.sh` (sources + all 3 bundles, both archs) | 0 failures |
| AU probe MIDI FX + Instrument (incl. 0.5.1 presets) | passed |
| VST3 host test (incl. 0.5.1 state) | passed |
| Steinberg validator 3.8.0_build_66 | 47 passed, 0 failed |
| Bundle signatures | Developer ID Application, hardened runtime, team 3A4R5EKM7V |
| `PackageInfo` | `relocatable="false"`, empty `<relocate/>`, the 3 bundles with the new ids at 0.6.0 |
| BOM | parents 40755 root:wheel, no 0700 entries |
| pkg | Developer ID Installer, trusted timestamp, notarized + stapled, `spctl -t install`: accepted, Notarized Developer ID |
| DMG | Developer ID Application, notarized + stapled, `spctl -t open`: accepted, Notarized Developer ID; contains pkg (same SHA-256), README, notices |

Outstanding before release: the real-DAW test (`Docs/DAW_TEST_0.6.0.md`),
Windows CI on `juce-free`, and Music-Prod Studio learning the new bundle ids.
