# ChordEngine 0.5.1 host-state fixtures

Recorded on 2026-10-22 from the shipped 0.5.1 binaries (build tree
`/tmp/ChordEngine-0.5.1-RC-20261022a/build`, the build that was signed,
notarized and published as release `c86688b6-…`), loaded in-process by
`Tools/StateCapture/CaptureLegacyState.mm` and `Tests/AUProbe.mm`. Nothing was
installed.

| File | What it is |
|---|---|
| `au-midifx-default.aupreset`, `au-instrument-default.aupreset` | The AU ClassInfo dictionary of a fresh 0.5.1 instance (what Logic stores in a project) |
| `au-midifx-custom.aupreset`, `au-instrument-custom.aupreset` | The ClassInfo after 0.5.1 restored a non-default configuration and wrote it back |
| `au-*-custom.state` | The `jucePluginState` bytes of those dictionaries (0.5.1's own XML, JUCE-wrapped lines, CRLF, character references) |
| `vst3-default.state`, `vst3-custom.state` | The VST3 component state of 0.5.1 (`IComponent::getState`), including the `JUCEPrivateData` trailer |
| `au-*-describe.json` | The 0.5.1 AUs' interface as `AUProbe --describe` reports it (buses, channel configurations, tail, MIDI output) |

The custom configuration: key D# (index 3), Dorian, Neo Soul, transpose target
lowest, octave -1, fixed velocity 77, a pack preset named `Café ♯` from pack
`Test & Pack` with an open voicing, internal audio on with the bass sound,
quantize 1/3 beat, FOLLOW DAW RECORD on.
