# Third-party source in ChordEngine

ChordEngine uses exactly two third-party code bases. Both are vendored here as
unmodified upstream source subsets, taken from the official repositories at
pinned tags (not from any other product's copy).

| Folder | Upstream | Version / commit | Licence | Ships in |
|---|---|---|---|---|
| `AudioUnitSDK/` | https://github.com/apple/AudioUnitSDK | tag `AudioUnitSDK-1.4.0`, commit `bd98b31feff57a15989fcfab4cd86dc63382b1ac` | Apache License 2.0 (`AudioUnitSDK/LICENSE.txt`) | the two Audio Units |
| `vst3sdk/` | https://github.com/steinbergmedia/vst3sdk | tag `v3.8.0_build_66`, commit `9fad9770f2ae8542ab1a548a68c1ad1ac690abe0`; submodules `pluginterfaces` `31d6eeba6daaa3e2a8bfbe3e7a90ca0b7fbfbc1c`, `base` `3d2e82f8e6bff59c1d8b7a27491a29c2286b5206`, `public.sdk` `a3911a4615dabbfdfd9d181ee26b05c70c289a95` | MIT License (`vst3sdk/LICENSE.txt`, and each submodule's `LICENSE.txt`) | the VST3 |

What was copied:

- `AudioUnitSDK/`: `include/`, `src/` and `LICENSE.txt`, unchanged.
- `vst3sdk/`: `pluginterfaces/` and `base/` (without their tests and CMake
  files), and from `public.sdk/source` only the files the VST3 adapter compiles
  (`common/memorystream`, `common/pluginview`, `common/commonstringconvert`,
  `vst/vstbus`, `vst/vstcomponent`, `vst/vstcomponentbase`, `vst/vstinitiids`,
  `vst/vstparameters`, `vst/vsteditcontroller`, `vst/utility/stringconvert`,
  `vst/utility/optional.h`), plus the licence files. No file is modified.

Both licences allow selling closed-source plug-ins. Apache-2.0 requires the
licence text and attribution to travel with the binaries (see
`Docs/THIRD_PARTY_NOTICES.txt`); MIT requires the copyright notice and licence
text. "VST" is a Steinberg trademark: its use in names and logos follows
Steinberg's VST 3 Usage Guidelines (`VST3_Usage_Guidelines.pdf` in the SDK),
which are separate from the code licence.

The Steinberg validator used by the tests is built from the full SDK at the
same tag in `Build/sdk-src/` (fetched by `Tools/FetchSdk.sh`); it is a test
tool and never ships.
