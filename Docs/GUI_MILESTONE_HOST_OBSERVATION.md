# Native AU Logic host observation — routing milestone

**Evidence class:** manual human-host observation; not automated DAW testing.

Martin tested the native ChordEngine AU on a clean second Mac in Logic Pro and confirmed:

- Logic recognizes **Music-Prod → ChordEngine** as a MIDI FX.
- ChordEngine receives MIDI input.
- A valid trigger note generates the expected chord.
- Generated chord MIDI reaches the downstream Classic Electric Piano/software instrument.

This confirms the critical AU MIDI routing and chord generation for the tested case. It does **not** establish parity for all 38 fixture vectors, GUI appearance or controls, resizing, VST3 hosts, every note range/controller, or all host conditions. Do not describe it as automated verification.

A structured record is maintained in [HostObservations.json](../Tests/HostObservations.json) with `hostObserved: true`. The 38 source-derived vectors in [FLReferenceVectors.json](../Tests/FLReferenceVectors.json) remain `hostObserved: false` and keep their `VERIFIED-SOURCE-STATIC` evidence class.

The GUI-specific second-Mac test plan is [SECOND_MAC_GUI_TEST.md](SECOND_MAC_GUI_TEST.md); it rechecks MIDI routing after evaluating the GUI while separately covering selectors, visual piano state, transpose, velocity, window visibility, and known placeholder limitations.

## Current development binaries

The requested fresh AU and VST3 GUI test archives are listed in
[MANIFEST.txt](../Build/TestArtifacts/MANIFEST.txt). They are development-only test
artifacts (not distribution packages); no install on this development Mac, signing,
notarization, or publication was performed.
